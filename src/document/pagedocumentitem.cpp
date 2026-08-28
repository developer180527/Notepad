#include "document/pagedocumentitem.h"

#include "util/theme.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QDateTime>
#include <QFileInfo>
#include <QFocusEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneDragDropEvent>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPalette>
#include <QStyleOptionGraphicsItem>
#include <QTextBlock>
#include <QTextDocumentFragment>
#include <QTextFragment>
#include <QTextFrame>
#include <QTextImageFormat>
#include <QTextLayout>
#include <QTextTable>
#include <QTextTableCell>
#include <QTextTableCellFormat>
#include <QUrl>
#include <QTimer>
#include <QUrl>

#include <cmath>

PageDocumentItem::PageDocumentItem(QGraphicsItem *parent)
    : QGraphicsObject(parent)
{
    setFlag(QGraphicsItem::ItemIsFocusable, true);
    setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true); // exposedRect for culling
    setFlag(QGraphicsItem::ItemAcceptsInputMethod, true);
    setAcceptHoverEvents(true);
    setAcceptDrops(true);
    setCursor(Qt::IBeamCursor);

    // A4 portrait @ 96 dpi with 25 mm margins by default.
    const qreal pxPerMm = 96.0 / 25.4;
    m_sheetSize = QSizeF(210 * pxPerMm, 297 * pxPerMm);
    const qreal m = 25 * pxPerMm;
    m_margins = QMarginsF(m, m, m, m);

    m_doc = new QTextDocument(this);
    m_doc->setDocumentMargin(0);
    m_doc->setUndoRedoEnabled(true);
    m_doc->setDefaultFont(QFont(QStringLiteral("Helvetica"), 12));
    m_doc->setPageSize(QSizeF(textW(), textH()));

    m_cursor = QTextCursor(m_doc);
    m_typingFormat = m_cursor.charFormat();

    connect(m_doc, &QTextDocument::contentsChanged, this, [this] {
        recomputeSearchMatches();   // keep highlight-all accurate while editing
        recomputePages();
        emit contentsChanged();
    });
    connect(m_doc->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
            this, [this](const QSizeF &) { recomputePages(); });
    connect(m_doc, &QTextDocument::undoAvailable, this, &PageDocumentItem::undoAvailable);
    connect(m_doc, &QTextDocument::redoAvailable, this, &PageDocumentItem::redoAvailable);

    // Document *property* changes (setDocumentMargin, setDefaultFont, …) push
    // real entries onto the undo stack, so the setup above leaves a phantom step
    // behind: a brand-new document would show Undo enabled, and the last Ctrl+Z
    // after an edit would appear to do nothing. Start from a clean history.
    m_doc->clearUndoRedoStacks();

    m_blink = new QTimer(this);
    m_blink->setInterval(530);
    connect(m_blink, &QTimer::timeout, this, [this] {
        m_caretOn = !m_caretOn;
        update(caretSceneRect().adjusted(-2, -2, 2, 2));
    });

    recomputePages();
}

// --------------------------------------------------------------- geometry

QRectF PageDocumentItem::sheetRect(int page) const
{
    const qreal top = page * (m_sheetSize.height() + m_gap);
    return QRectF(0, top, m_sheetSize.width(), m_sheetSize.height());
}

QRectF PageDocumentItem::textRect(int page) const
{
    const QRectF s = sheetRect(page);
    return QRectF(s.left() + m_margins.left(), s.top() + m_margins.top(), textW(), textH());
}

QRectF PageDocumentItem::boundingRect() const
{
    const qreal total = m_pageCount * (m_sheetSize.height() + m_gap) - m_gap;
    return QRectF(-2, -2, m_sheetSize.width() + 16, total + 16); // room for shadow
}

void PageDocumentItem::documentRangeFor(const QRectF &itemRect, int *from, int *to) const
{
    if (from) *from = 0;
    if (to)   *to = 0;

    const qreal stride = m_sheetSize.height() + m_gap;
    const qreal th = textH();
    if (stride <= 0 || th <= 0 || m_pageCount < 1)
        return;

    // The rect can be empty or unnormalised before the view has a size, so
    // clamp explicitly rather than assuming top <= bottom.
    const QRectF r = itemRect.normalized();
    const int maxPage = m_pageCount - 1;
    int firstPage = r.isEmpty() ? 0 : int(std::floor(r.top() / stride));
    int lastPage = r.isEmpty() ? maxPage : int(std::floor(r.bottom() / stride));
    firstPage = qMax(0, qMin(firstPage, maxPage));
    lastPage = qMax(firstPage, qMin(lastPage, maxPage));

    QAbstractTextDocumentLayout *layout = m_doc->documentLayout();
    // hitTest in document space: pages are laid out as one continuous column of
    // height textH() each, which is exactly how paint() slices them.
    if (from)
        *from = layout->hitTest(QPointF(0, firstPage * th), Qt::FuzzyHit);
    if (to)
        *to = layout->hitTest(QPointF(textW(), (lastPage + 1) * th), Qt::FuzzyHit);
}

void PageDocumentItem::recomputePages()
{
    // Note: do NOT setPageSize here — it can force a full document relayout, and
    // this runs on every edit. The page size only changes via Page Setup
    // (setPageLayoutMetrics) and the constructor. pageCount() already reflects
    // the current content under the fixed page size.
    const int pc = qMax(1, m_doc->pageCount());
    if (pc != m_pageCount) {
        prepareGeometryChange();
        m_pageCount = pc;
        emit pageCountChanged(pc);
    }
    update();
}

void PageDocumentItem::setPageLayoutMetrics(const QSizeF &sheetPx, const QMarginsF &marginsPx,
                                            qreal gapPx)
{
    prepareGeometryChange();
    m_sheetSize = sheetPx;
    m_margins = marginsPx;
    m_gap = gapPx;
    m_doc->setPageSize(QSizeF(textW(), textH()));   // only when the layout changes
    fitContentToPageWidth();      // a narrower page may now be too small for an image
    recomputePages();
    emit ensureVisibleRequested(caretSceneRect());
}

// --------------------------------------------------------------- painting

// A page's static content: text plus search highlights. A live selection is
// excluded — it changes constantly while dragging, and caching it would mean
// re-rendering the page on every mouse move — so a selected page is drawn
// directly instead.
void PageDocumentItem::drawPageContent(QPainter *painter, int page, const QRectF &tr,
                                      const QRectF &exposed)
{
    const qreal th = textH();
    const Theme &theme = Theme::instance();

    QPalette pal;
    pal.setColor(QPalette::Text, theme.pageTextColor());

    QTextCharFormat searchFmt;
    searchFmt.setBackground(QColor(255, 230, 64));
    searchFmt.setForeground(Qt::black);

    // Restricting the clip to what is actually on screen lets the layout skip
    // blocks entirely. At high zoom only a sliver of the page is visible, so
    // this is the difference between laying out one screenful and a whole page.
    auto pageClip = [&](bool wholePage) {
        const QRectF full(0, page * th, textW(), th);
        if (wholePage || !exposed.isValid())
            return full;
        const QRectF vis = exposed.intersected(tr);
        if (!vis.isValid())
            return full;
        return QRectF(vis.left() - tr.left(), vis.top() - tr.top() + page * th,
                      vis.width(), vis.height()).intersected(full);
    };

    auto renderInto = [&](QPainter *p, bool withSelection, bool wholePage) {
        QAbstractTextDocumentLayout::PaintContext ctx;
        ctx.palette = pal;
        ctx.clip = pageClip(wholePage);
        for (const QTextCursor &match : m_matchesByPage.value(page)) {
            QAbstractTextDocumentLayout::Selection sel;
            sel.cursor = match;
            sel.format = searchFmt;
            ctx.selections.append(sel);
        }
        if (withSelection && m_cursor.hasSelection()) {
            QAbstractTextDocumentLayout::Selection sel;
            sel.cursor = m_cursor;
            sel.format.setBackground(QApplication::palette().color(QPalette::Highlight));
            sel.format.setForeground(QApplication::palette().color(QPalette::HighlightedText));
            ctx.selections.append(sel);
        }
        m_doc->documentLayout()->draw(p, ctx);
    };

    // Selection present on this page: bypass the cache entirely.
    const bool selectionHere = m_cursor.hasSelection()
        && pageForPosition(m_cursor.selectionStart()) <= page
        && pageForPosition(m_cursor.selectionEnd()) >= page;
    if (selectionHere) {
        painter->save();
        painter->setClipRect(tr);
        painter->translate(tr.topLeft());
        painter->translate(0, -page * th);
        renderInto(painter, true, false);
        painter->restore();
        return;
    }

    painter->save();
    painter->setClipRect(tr);
    painter->translate(tr.topLeft());
    painter->translate(0, -page * th);
    renderInto(painter, false, false);
    painter->restore();
}

void PageDocumentItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                             QWidget *)
{
    const QRectF exposed = option->exposedRect;
    const qreal stride = m_sheetSize.height() + m_gap;
    const qreal th = textH();

    int first = static_cast<int>(std::floor(exposed.top() / stride));
    int last = static_cast<int>(std::floor(exposed.bottom() / stride));
    first = qBound(0, first, m_pageCount - 1);
    last = qBound(0, last, m_pageCount - 1);

    const Theme &theme = Theme::instance();
    QPalette pal;
    pal.setColor(QPalette::Text, theme.pageTextColor());
    const QColor highlight = QApplication::palette().color(QPalette::Highlight);
    const QColor highlightText = QApplication::palette().color(QPalette::HighlightedText);

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    QTextCharFormat searchFmt;
    searchFmt.setBackground(QColor(255, 230, 64));
    searchFmt.setForeground(Qt::black);

    for (int page = first; page <= last; ++page) {
        const QRectF sheet = sheetRect(page);

        // Soft shadow, then the white sheet.
        painter->setPen(Qt::NoPen);
        for (int i = 5; i >= 1; --i) {
            painter->setBrush(QColor(0, 0, 0, 8));
            painter->drawRoundedRect(sheet.adjusted(-i + 3, -i + 4, i + 3, i + 5), 2, 2);
        }
        painter->setBrush(theme.pageColor());
        painter->setPen(QPen(theme.pageBorderColor(), 1));
        painter->drawRect(sheet);

        // Draw this page's slice of the document into its text rect.
        const QRectF tr = textRect(page);
        drawPageContent(painter, page, tr, exposed);

        // The caret is drawn here rather than by the layout, so that a blink
        // never invalidates a cached page.
        if (m_focused && m_caretOn && !m_cursor.hasSelection()
            && pageForPosition(m_cursor.position()) == page) {
            const QRectF caret = caretSceneRect();
            if (caret.isValid()) {
                painter->save();
                painter->setClipRect(tr);
                painter->fillRect(QRectF(caret.left(), caret.top(), 1.0, caret.height()),
                                  pal.color(QPalette::Text));
                painter->restore();
            }
        }
    }

    // Selection chrome for an image: a border plus 8 resize handles.
    if (m_focused && m_selectedImagePos >= 0) {
        const QRectF r = imageItemRect(m_selectedImagePos);
        if (r.isValid()) {
            const QColor accent(0x37, 0x8A, 0xDD);
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(accent, 1.5));
            painter->drawRect(r);
            painter->setPen(QPen(accent.darker(120), 1));
            painter->setBrush(Qt::white);
            for (int i : {3, 4, 5}) {
                const QPointF p = handlePoint(r, i);
                painter->drawRect(QRectF(p.x() - 4, p.y() - 4, 8, 8));
            }
        }
    }
}

// --------------------------------------------------------------- cursor helpers

int PageDocumentItem::documentPositionAt(const QPointF &itemPos) const
{
    const qreal stride = m_sheetSize.height() + m_gap;
    int page = static_cast<int>(std::floor(itemPos.y() / stride));
    page = qBound(0, page, m_pageCount - 1);

    const QRectF tr = textRect(page);
    qreal lx = qBound(0.0, itemPos.x() - tr.left(), textW());
    qreal ly = qBound(0.0, itemPos.y() - tr.top(), textH());
    const QPointF docPoint(lx, page * textH() + ly);
    return m_doc->documentLayout()->hitTest(docPoint, Qt::FuzzyHit);
}

QRectF PageDocumentItem::caretSceneRect() const
{
    const QTextBlock block = m_cursor.block();
    if (!block.isValid())
        return QRectF(0, 0, 1, 1);

    const QRectF blockRect = m_doc->documentLayout()->blockBoundingRect(block);
    qreal docX = blockRect.left();
    qreal docY = blockRect.top();
    qreal h = blockRect.height();

    if (QTextLayout *layout = block.layout()) {
        const int posInBlock = m_cursor.position() - block.position();
        const QTextLine line = layout->lineForTextPosition(posInBlock);
        if (line.isValid()) {
            docX = blockRect.left() + line.cursorToX(posInBlock);
            docY = blockRect.top() + line.y();
            h = line.height();
        }
    }

    const qreal th = textH();
    int page = qBound(0, static_cast<int>(docY / th), m_pageCount - 1);
    const QRectF tr = textRect(page);
    return QRectF(tr.left() + docX, tr.top() + (docY - page * th), 2.0, h);
}

int PageDocumentItem::currentPage() const
{
    const QRectF caret = caretSceneRect();
    const qreal stride = m_sheetSize.height() + m_gap;
    return qBound(1, static_cast<int>(caret.top() / stride) + 1, m_pageCount);
}


// --------------------------------------------------------------- image helpers


// --------------------------------------------------------------- editing API


// --------------------------------------------------------------- find / replace


// --------------------------------------------------------------- events


// --------------------------------------------------------------- drag & drop


