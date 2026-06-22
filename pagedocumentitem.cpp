#include "pagedocumentitem.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
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
    recomputePages();
    emit ensureVisibleRequested(caretSceneRect());
}

// --------------------------------------------------------------- painting

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

    QPalette pal;
    pal.setColor(QPalette::Text, Qt::black);
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
        painter->setBrush(Qt::white);
        painter->setPen(QPen(QColor(208, 208, 208), 1));
        painter->drawRect(sheet);

        // Draw this page's slice of the document into its text rect.
        const QRectF tr = textRect(page);
        painter->save();
        painter->setClipRect(tr);
        painter->translate(tr.topLeft());
        painter->translate(0, -page * th);

        QAbstractTextDocumentLayout::PaintContext ctx;
        ctx.palette = pal;
        ctx.clip = QRectF(0, page * th, textW(), th);
        if (m_focused && m_caretOn && !m_cursor.hasSelection())
            ctx.cursorPosition = m_cursor.position();

        // Find: highlight this page's matches in yellow (drawn first, under the
        // caret selection so the active match still stands out).
        const QList<QTextCursor> pageMatches = m_matchesByPage.value(page);
        for (const QTextCursor &match : pageMatches) {
            QAbstractTextDocumentLayout::Selection sel;
            sel.cursor = match;
            sel.format = searchFmt;
            ctx.selections.append(sel);
        }
        if (m_cursor.hasSelection()) {
            QAbstractTextDocumentLayout::Selection sel;
            sel.cursor = m_cursor;
            sel.format.setBackground(highlight);
            sel.format.setForeground(highlightText);
            ctx.selections.append(sel);
        }
        m_doc->documentLayout()->draw(painter, ctx);
        painter->restore();
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

void PageDocumentItem::notifyCursorUi()
{
    emit cursorPositionChanged();
    emit selectionAvailable(m_cursor.hasSelection());
}

void PageDocumentItem::afterCursorMoved()
{
    m_selectedImagePos = -1;       // any caret move / edit drops the image selection
    m_typingFormat = m_cursor.charFormat();
    m_caretOn = true;
    notifyCursorUi();
    emit ensureVisibleRequested(caretSceneRect());
    update();
}

void PageDocumentItem::setCursorAndNotify(const QTextCursor &cursor)
{
    m_cursor = cursor;
    afterCursorMoved();
}

// --------------------------------------------------------------- image helpers

QTextImageFormat PageDocumentItem::imageFormatAt(int pos) const
{
    const QTextBlock block = m_doc->findBlock(pos);
    if (!block.isValid())
        return QTextImageFormat();
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment frag = it.fragment();
        if (!frag.isValid() || !frag.charFormat().isImageFormat())
            continue;
        if (pos >= frag.position() && pos < frag.position() + frag.length())
            return frag.charFormat().toImageFormat();
    }
    return QTextImageFormat();
}

QRectF PageDocumentItem::docRectToItem(const QRectF &d) const
{
    const qreal th = textH();
    const int page = qBound(0, static_cast<int>(d.top() / th), m_pageCount - 1);
    const QRectF tr = textRect(page);
    return QRectF(tr.left() + d.left(), tr.top() + (d.top() - page * th), d.width(), d.height());
}

QRectF PageDocumentItem::imageItemRect(int imagePos) const
{
    const QTextBlock block = m_doc->findBlock(imagePos);
    if (!block.isValid())
        return QRectF();

    QTextImageFormat fmt = imageFormatAt(imagePos);
    qreal w = fmt.width();
    qreal h = fmt.height();
    if (w <= 0 || h <= 0) {
        const QImage im = m_doc->resource(QTextDocument::ImageResource,
                                          QUrl(fmt.name())).value<QImage>();
        if (!im.isNull()) {
            if (w <= 0) w = im.width();
            if (h <= 0) h = im.height();
        }
    }

    // Floating image: take its rect from the laid-out frame.
    if (QTextFrame *frame = imageFrameAt(imagePos)) {
        const qreal m = frame->frameFormat().margin();
        const QRectF fr = m_doc->documentLayout()->frameBoundingRect(frame);
        return docRectToItem(QRectF(fr.left() + m, fr.top() + m, w, h));
    }

    const QRectF blockRect = m_doc->documentLayout()->blockBoundingRect(block);
    qreal docX = blockRect.left();
    qreal docY = blockRect.top();
    if (QTextLayout *layout = block.layout()) {
        const int inBlock = imagePos - block.position();
        const QTextLine line = layout->lineForTextPosition(inBlock);
        if (line.isValid()) {
            docX = blockRect.left() + line.cursorToX(inBlock);
            docY = blockRect.top() + line.y();
            const qreal extra = line.height() - h;   // inline image sits on the baseline
            if (extra > 0)
                docY += extra;
        }
    }
    return docRectToItem(QRectF(docX, docY, w, h));
}

QTextFrame *PageDocumentItem::imageFrameAt(int pos) const
{
    const auto frames = m_doc->rootFrame()->childFrames();
    for (QTextFrame *f : frames) {
        if (f->frameFormat().position() != QTextFrameFormat::InFlow
            && pos >= f->firstPosition() && pos <= f->lastPosition())
            return f;
    }
    return nullptr;
}

int PageDocumentItem::imageAt(const QPointF &itemPos) const
{
    // Inline images in the block under the point.
    const QTextBlock block = m_doc->findBlock(documentPositionAt(itemPos));
    if (block.isValid()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (frag.isValid() && frag.charFormat().isImageFormat()
                && imageItemRect(frag.position()).contains(itemPos))
                return frag.position();
        }
    }
    // Floating images live in child frames, outside the normal block flow.
    const auto frames = m_doc->rootFrame()->childFrames();
    for (QTextFrame *frame : frames) {
        if (frame->frameFormat().position() == QTextFrameFormat::InFlow)
            continue;
        const int imgPos = imageInFrame(frame);
        if (imgPos >= 0 && imageItemRect(imgPos).contains(itemPos))
            return imgPos;
    }
    return -1;
}

int PageDocumentItem::imageInFrame(QTextFrame *frame) const
{
    if (!frame)
        return -1;
    for (auto it = frame->begin(); !it.atEnd(); ++it) {
        const QTextBlock b = it.currentBlock();
        if (!b.isValid())
            continue;
        for (auto fit = b.begin(); !fit.atEnd(); ++fit) {
            const QTextFragment frag = fit.fragment();
            if (frag.isValid() && frag.charFormat().isImageFormat())
                return frag.position();
        }
    }
    return -1;
}

PageDocumentItem::WrapMode PageDocumentItem::wrapModeOf(int imagePos) const
{
    QTextFrame *f = imageFrameAt(imagePos);
    if (!f)
        return WrapMode::Inline;
    return f->frameFormat().position() == QTextFrameFormat::FloatRight ? WrapMode::FloatRight
                                                                       : WrapMode::FloatLeft;
}

void PageDocumentItem::changeWrapMode(int imagePos, WrapMode mode)
{
    const WrapMode current = wrapModeOf(imagePos);
    if (current == mode)
        return;
    QTextImageFormat fmt = imageFormatAt(imagePos);
    if (fmt.name().isEmpty())
        return;

    m_cursor.beginEditBlock();
    QTextFrame *frame = imageFrameAt(imagePos);

    if (mode == WrapMode::Inline) {                 // float -> inline
        QTextCursor c(m_doc);
        c.setPosition(frame->firstPosition() - 1);
        c.setPosition(frame->lastPosition() + 1, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.insertImage(fmt);
        m_selectedImagePos = c.position() - 1;
    } else if (current == WrapMode::Inline) {        // inline -> float
        QTextCursor c(m_doc);
        c.setPosition(imagePos);
        c.setPosition(imagePos + 1, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        QTextFrameFormat ff;
        ff.setPosition(mode == WrapMode::FloatLeft ? QTextFrameFormat::FloatLeft
                                                   : QTextFrameFormat::FloatRight);
        ff.setBorder(0);
        ff.setMargin(8);
        ff.setWidth(QTextLength(QTextLength::FixedLength, fmt.width() > 0 ? fmt.width() : 120));
        QTextFrame *nf = c.insertFrame(ff);
        nf->firstCursorPosition().insertImage(fmt);
        m_selectedImagePos = imageInFrame(nf);
    } else {                                         // float-left <-> float-right
        QTextFrameFormat ff = frame->frameFormat();
        ff.setPosition(mode == WrapMode::FloatLeft ? QTextFrameFormat::FloatLeft
                                                   : QTextFrameFormat::FloatRight);
        frame->setFrameFormat(ff);
        m_selectedImagePos = imagePos;
    }

    m_cursor.endEditBlock();
    recomputePages();
    emit contentsChanged();
    notifyCursorUi();
    update();
}

// Only the right / bottom / corner handles resize sanely (a flowed image's
// top-left is pinned by the layout), so we expose just those three.
QPointF PageDocumentItem::handlePoint(const QRectF &r, int index)
{
    switch (index) {
    case 3:  return QPointF(r.right(), r.center().y());   // right edge
    case 5:  return QPointF(r.center().x(), r.bottom());  // bottom edge
    default: return r.bottomRight();                      // 4: corner
    }
}

int PageDocumentItem::handleAt(const QPointF &itemPos) const
{
    if (m_selectedImagePos < 0)
        return -1;
    const QRectF r = imageItemRect(m_selectedImagePos);
    for (int i : {4, 3, 5}) {   // prefer the corner
        const QPointF p = handlePoint(r, i);
        if (QRectF(p.x() - 7, p.y() - 7, 14, 14).contains(itemPos))
            return i;
    }
    return -1;
}

void PageDocumentItem::applyImageSize(int imagePos, qreal w, qreal h)
{
    QTextImageFormat fmt = imageFormatAt(imagePos);
    if (fmt.name().isEmpty())
        return;
    w = qMax(16.0, w);
    h = qMax(16.0, h);
    fmt.setWidth(qRound(w));
    fmt.setHeight(qRound(h));
    QTextCursor c(m_doc);
    c.setPosition(imagePos);
    c.setPosition(imagePos + 1, QTextCursor::KeepAnchor);
    c.setCharFormat(fmt);
    // Keep the floating frame box in step with the image.
    if (QTextFrame *frame = imageFrameAt(imagePos)) {
        QTextFrameFormat ff = frame->frameFormat();
        ff.setWidth(QTextLength(QTextLength::FixedLength, qRound(w)));
        frame->setFrameFormat(ff);
    }
    update();
}

void PageDocumentItem::clearImageSelection()
{
    if (m_selectedImagePos >= 0) {
        m_selectedImagePos = -1;
        update();
    }
}

void PageDocumentItem::documentReset()
{
    m_cursor = QTextCursor(m_doc);
    m_typingFormat = m_cursor.charFormat();
    recomputePages();
    notifyCursorUi();
    emit ensureVisibleRequested(caretSceneRect());
    update();
}

// --------------------------------------------------------------- editing API

void PageDocumentItem::undo()
{
    m_doc->undo(&m_cursor);
    afterCursorMoved();
}

void PageDocumentItem::redo()
{
    m_doc->redo(&m_cursor);
    afterCursorMoved();
}

void PageDocumentItem::copy()
{
    if (!m_cursor.hasSelection())
        return;
    const QTextDocumentFragment fragment = m_cursor.selection();
    auto *mime = new QMimeData;
    mime->setText(fragment.toPlainText());
    mime->setHtml(fragment.toHtml());
    QApplication::clipboard()->setMimeData(mime);
}

void PageDocumentItem::cut()
{
    if (!m_cursor.hasSelection())
        return;
    copy();
    m_cursor.removeSelectedText();
    afterCursorMoved();
}

void PageDocumentItem::paste()
{
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (!mime)
        return;
    if (mime->hasImage()) {
        const QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            insertImage(img);
            return;
        }
    }
    if (mime->hasHtml())
        m_cursor.insertHtml(mime->html());
    else if (mime->hasText())
        m_cursor.insertText(mime->text(), m_typingFormat);
    afterCursorMoved();
}

void PageDocumentItem::selectAll()
{
    m_cursor.movePosition(QTextCursor::Start);
    m_cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    notifyCursorUi();
    update();
}

void PageDocumentItem::mergeFormatOnSelection(const QTextCharFormat &format)
{
    if (m_cursor.hasSelection()) {
        m_cursor.mergeCharFormat(format);
        m_typingFormat = m_cursor.charFormat();
    } else {
        // No selection: set the pending "typing format" so the next characters
        // use it — lets you choose formatting before typing in a fresh doc.
        m_typingFormat.merge(format);
    }
    notifyCursorUi();
    update();
}

void PageDocumentItem::setAlignmentValue(Qt::Alignment alignment)
{
    QTextBlockFormat bf;
    bf.setAlignment(alignment);
    m_cursor.mergeBlockFormat(bf);
    notifyCursorUi();
    update();
}

Qt::Alignment PageDocumentItem::alignmentValue() const
{
    return m_cursor.blockFormat().alignment();
}

void PageDocumentItem::setBaseFont(const QFont &font)
{
    m_doc->setDefaultFont(font);
    recomputePages();
}

void PageDocumentItem::insertImage(const QImage &image)
{
    if (image.isNull())
        return;

    // Keep the image at (near) full resolution so it stays crisp when shown
    // small or zoomed in — only the *display* size is fit to the page width.
    // A generous cap bounds memory/file size for very large originals.
    constexpr int kMaxStored = 3000;
    QImage stored = image;
    if (stored.width() > kMaxStored)
        stored = stored.scaledToWidth(kMaxStored, Qt::SmoothTransformation);

    const QString name = QStringLiteral("img://%1.png").arg(QDateTime::currentMSecsSinceEpoch());
    m_doc->addResource(QTextDocument::ImageResource, QUrl(name), stored);

    qreal w = stored.width();
    qreal h = stored.height();
    const qreal maxW = textW();
    if (maxW > 0 && w > maxW) {
        h *= maxW / w;       // fit to text width, preserving aspect
        w = maxW;
    }

    QTextImageFormat fmt;
    fmt.setName(name);
    fmt.setWidth(w);
    fmt.setHeight(h);
    m_cursor.insertImage(fmt);
    afterCursorMoved();
}

void PageDocumentItem::insertTable(int rows, int columns)
{
    if (rows < 1 || columns < 1)
        return;
    QTextTableFormat fmt;
    fmt.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    fmt.setBorder(1);
    fmt.setBorderBrush(QColor(150, 150, 150));
    fmt.setBorderCollapse(true);      // single-line grid (no doubled borders)
    fmt.setCellPadding(4);
    fmt.setCellSpacing(0);
    fmt.setWidth(QTextLength(QTextLength::PercentageLength, 100));   // fill the text width
    QList<QTextLength> widths;
    widths.reserve(columns);
    for (int i = 0; i < columns; ++i)
        widths << QTextLength(QTextLength::PercentageLength, 100.0 / columns);
    fmt.setColumnWidthConstraints(widths);

    m_cursor.insertTable(rows, columns, fmt);
    afterCursorMoved();
}

QImage PageDocumentItem::renderPreview(int maxWidthPx) const
{
    const qreal sheetW = m_sheetSize.width();
    const qreal sheetH = m_sheetSize.height();
    if (sheetW <= 0 || sheetH <= 0 || maxWidthPx <= 0)
        return QImage();

    const qreal scale = maxWidthPx / sheetW;
    QImage img(QSize(maxWidthPx, qRound(sheetH * scale)), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.scale(scale, scale);

    // The white sheet with a hairline border.
    p.setBrush(Qt::white);
    p.setPen(QPen(QColor(220, 220, 220), 1));
    p.drawRect(QRectF(0, 0, sheetW, sheetH));

    // Page 1's slice of the document, inset by the margins.
    const QRectF tr(m_margins.left(), m_margins.top(), textW(), textH());
    p.save();
    p.setClipRect(tr);
    p.translate(tr.topLeft());
    QAbstractTextDocumentLayout::PaintContext ctx;
    QPalette pal;
    pal.setColor(QPalette::Text, Qt::black);
    ctx.palette = pal;
    ctx.clip = QRectF(0, 0, textW(), textH());
    m_doc->documentLayout()->draw(&p, ctx);
    p.restore();
    p.end();
    return img;
}

// --------------------------------------------------------------- find / replace

void PageDocumentItem::setSearchHighlight(const QString &text, QTextDocument::FindFlags flags)
{
    m_searchText = text;
    m_searchFlags = flags;
    recomputeSearchMatches();
    update();
}

void PageDocumentItem::recomputeSearchMatches()
{
    m_matchesByPage.clear();
    if (m_searchText.isEmpty())
        return;
    const QTextDocument::FindFlags flags = m_searchFlags & ~QTextDocument::FindBackward;
    QTextCursor c(m_doc);
    int guard = 0;
    forever {
        c = m_doc->find(m_searchText, c, flags);
        if (c.isNull())
            break;
        m_matchesByPage[pageForPosition(c.selectionStart())].append(c);
        if (++guard >= 5000)   // safety cap for pathological documents
            break;
    }
}

int PageDocumentItem::pageForPosition(int pos) const
{
    const QTextBlock block = m_doc->findBlock(pos);
    if (!block.isValid())
        return 0;
    qreal y = m_doc->documentLayout()->blockBoundingRect(block).top();
    if (QTextLayout *layout = block.layout()) {
        const QTextLine line = layout->lineForTextPosition(pos - block.position());
        if (line.isValid())
            y += line.y();
    }
    const qreal th = textH();
    return th > 0 ? qMax(0, static_cast<int>(y / th)) : 0;
}

bool PageDocumentItem::find(const QString &text, QTextDocument::FindFlags flags)
{
    if (text.isEmpty())
        return false;
    QTextCursor found = m_doc->find(text, m_cursor, flags);
    if (found.isNull()) {
        // Wrap around.
        QTextCursor start(m_doc);
        if (flags & QTextDocument::FindBackward)
            start.movePosition(QTextCursor::End);
        found = m_doc->find(text, start, flags);
    }
    if (found.isNull())
        return false;
    setCursorAndNotify(found);
    return true;
}

bool PageDocumentItem::replaceSelection(const QString &text, const QString &with,
                                        QTextDocument::FindFlags flags)
{
    const bool cs = flags & QTextDocument::FindCaseSensitively;
    if (m_cursor.hasSelection()
        && QString::compare(m_cursor.selectedText(), text,
                            cs ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0) {
        m_cursor.insertText(with);
        afterCursorMoved();
    }
    return find(text, flags);
}

int PageDocumentItem::replaceAll(const QString &text, const QString &with,
                                 QTextDocument::FindFlags flags)
{
    if (text.isEmpty())
        return 0;
    int count = 0;
    QTextCursor editor(m_doc);
    editor.beginEditBlock();
    QTextCursor search(m_doc);
    search.movePosition(QTextCursor::Start);
    QTextDocument::FindFlags forward = flags & ~QTextDocument::FindBackward;
    forever {
        search = m_doc->find(text, search, forward);
        if (search.isNull())
            break;
        search.insertText(with);
        ++count;
    }
    editor.endEditBlock();
    if (count > 0) {
        m_typingFormat = m_cursor.charFormat();
        notifyCursorUi();
        update();
    }
    return count;
}

// --------------------------------------------------------------- events

void PageDocumentItem::keyPressEvent(QKeyEvent *event)
{
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    const bool ctrl = event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier);
    const QTextCursor::MoveMode mode = shift ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor;

    auto move = [&](QTextCursor::MoveOperation op) {
        m_cursor.movePosition(op, mode);
        afterCursorMoved();
        event->accept();
    };

    switch (event->key()) {
    case Qt::Key_Left:  move(ctrl ? QTextCursor::WordLeft : QTextCursor::Left); return;
    case Qt::Key_Right: move(ctrl ? QTextCursor::WordRight : QTextCursor::Right); return;
    case Qt::Key_Up:    move(QTextCursor::Up); return;
    case Qt::Key_Down:  move(QTextCursor::Down); return;
    case Qt::Key_Home:  move(ctrl ? QTextCursor::Start : QTextCursor::StartOfLine); return;
    case Qt::Key_End:   move(ctrl ? QTextCursor::End : QTextCursor::EndOfLine); return;
    case Qt::Key_PageUp:
        for (int i = 0; i < 20; ++i) m_cursor.movePosition(QTextCursor::Up, mode);
        afterCursorMoved(); event->accept(); return;
    case Qt::Key_PageDown:
        for (int i = 0; i < 20; ++i) m_cursor.movePosition(QTextCursor::Down, mode);
        afterCursorMoved(); event->accept(); return;
    case Qt::Key_Backspace:
        if (m_cursor.hasSelection()) m_cursor.removeSelectedText();
        else m_cursor.deletePreviousChar();
        afterCursorMoved(); event->accept(); return;
    case Qt::Key_Delete:
        if (m_cursor.hasSelection()) m_cursor.removeSelectedText();
        else m_cursor.deleteChar();
        afterCursorMoved(); event->accept(); return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        m_cursor.insertBlock();
        afterCursorMoved(); event->accept(); return;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        if (QTextTable *table = m_cursor.currentTable()) {
            const QTextTableCell cell = table->cellAt(m_cursor);
            int row = cell.row();
            int col = cell.column();
            if (event->key() == Qt::Key_Backtab || shift) {
                if (--col < 0) { col = table->columns() - 1; --row; }
            } else {
                if (++col >= table->columns()) {
                    col = 0;
                    if (++row >= table->rows())
                        table->appendRows(1);   // Tab past the last cell adds a row
                }
            }
            if (row >= 0) {
                m_cursor = table->cellAt(row, col).firstCursorPosition();
                afterCursorMoved();
            }
            event->accept();
            return;
        }
        m_cursor.insertText(QStringLiteral("\t"), m_typingFormat);
        afterCursorMoved(); event->accept(); return;
    default:
        break;
    }

    if (!ctrl && !event->text().isEmpty() && event->text().at(0).isPrint()) {
        m_cursor.insertText(event->text(), m_typingFormat);
        afterCursorMoved();
        event->accept();
        return;
    }
    event->ignore();
}

void PageDocumentItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    setFocus();

    // 1) Grabbing a resize handle of the already-selected image.
    const int handle = handleAt(event->pos());
    if (handle >= 0) {
        m_resizeHandle = handle;
        m_resizeStartRect = imageItemRect(m_selectedImagePos);
        m_resizeAspect = m_resizeStartRect.height() > 0
                             ? m_resizeStartRect.width() / m_resizeStartRect.height() : 1.0;
        m_cursor.beginEditBlock();   // group the whole drag into one undo step
        event->accept();
        return;
    }

    // 2) Clicking on an image selects it (handles appear).
    const int imgPos = imageAt(event->pos());
    if (imgPos >= 0) {
        m_selectedImagePos = imgPos;
        m_cursor.setPosition(imgPos);     // so alignment acts on the image's block
        m_typingFormat = m_cursor.charFormat();
        notifyCursorUi();
        update();
        event->accept();
        return;
    }

    // 3) Normal text caret / selection.
    const int pos = documentPositionAt(event->pos());
    if (event->modifiers() & Qt::ShiftModifier)
        m_cursor.setPosition(pos, QTextCursor::KeepAnchor);
    else
        m_cursor.setPosition(pos);
    m_selecting = true;
    afterCursorMoved();
    event->accept();
}

void PageDocumentItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_resizeHandle >= 0 && m_selectedImagePos >= 0) {
        const QRectF r = m_resizeStartRect;
        const bool freeRatio = event->modifiers() & Qt::ShiftModifier;
        const bool wide = m_resizeHandle == 0 || m_resizeHandle == 2 || m_resizeHandle == 3
                          || m_resizeHandle == 4 || m_resizeHandle == 6 || m_resizeHandle == 7;
        const bool tall = m_resizeHandle == 0 || m_resizeHandle == 1 || m_resizeHandle == 2
                          || m_resizeHandle == 4 || m_resizeHandle == 5 || m_resizeHandle == 6;
        const bool corner = wide && tall;

        qreal w = wide ? (event->pos().x() - r.left()) : r.width();
        qreal h = tall ? (event->pos().y() - r.top()) : r.height();
        w = qMax(16.0, w);
        h = qMax(16.0, h);
        if (corner && !freeRatio)
            h = w / m_resizeAspect;       // keep aspect from the dominant (width) axis

        applyImageSize(m_selectedImagePos, w, h);
        event->accept();
        return;
    }

    if (!m_selecting) {
        event->ignore();
        return;
    }
    m_cursor.setPosition(documentPositionAt(event->pos()), QTextCursor::KeepAnchor);
    notifyCursorUi();
    emit ensureVisibleRequested(caretSceneRect());
    update();
    event->accept();
}

void PageDocumentItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_resizeHandle >= 0) {
        m_resizeHandle = -1;
        m_cursor.endEditBlock();
        emit contentsChanged();           // mark modified
    }
    m_selecting = false;
    event->accept();
}

void PageDocumentItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    setFocus();
    m_cursor.setPosition(documentPositionAt(event->pos()));
    m_cursor.select(QTextCursor::WordUnderCursor);
    afterCursorMoved();
    event->accept();
}

void PageDocumentItem::focusInEvent(QFocusEvent *)
{
    m_focused = true;
    m_caretOn = true;
    m_blink->start();
    update(caretSceneRect().adjusted(-2, -2, 2, 2));
}

void PageDocumentItem::focusOutEvent(QFocusEvent *)
{
    m_focused = false;
    m_blink->stop();
    update();
}

void PageDocumentItem::hoverMoveEvent(QGraphicsSceneHoverEvent *)
{
    // I-beam cursor is set once in the constructor; nothing to do per move.
}

void PageDocumentItem::inputMethodEvent(QInputMethodEvent *event)
{
    if (!event->commitString().isEmpty()) {
        m_cursor.insertText(event->commitString(), m_typingFormat);
        afterCursorMoved();
    }
    event->accept();
}

QVariant PageDocumentItem::inputMethodQuery(Qt::InputMethodQuery query) const
{
    switch (query) {
    case Qt::ImCursorRectangle: return caretSceneRect();
    case Qt::ImFont:            return m_typingFormat.font();
    case Qt::ImCursorPosition:  return m_cursor.position();
    case Qt::ImSurroundingText: return m_cursor.block().text();
    case Qt::ImCurrentSelection:return m_cursor.selectedText();
    default:                    return QVariant();
    }
}

// --------------------------------------------------------------- drag & drop

static bool mimeHasUsableContent(const QMimeData *mime)
{
    if (mime->hasImage() || mime->hasUrls())
        return true;
    return false;
}

void PageDocumentItem::dragEnterEvent(QGraphicsSceneDragDropEvent *event)
{
    if (mimeHasUsableContent(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void PageDocumentItem::dragMoveEvent(QGraphicsSceneDragDropEvent *event)
{
    if (mimeHasUsableContent(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void PageDocumentItem::dropEvent(QGraphicsSceneDragDropEvent *event)
{
    const QMimeData *mime = event->mimeData();
    setFocus();
    m_cursor.setPosition(documentPositionAt(event->pos()));

    // A pasted/dragged bitmap.
    if (mime->hasImage()) {
        const QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            insertImage(img);
            event->acceptProposedAction();
            return;
        }
    }

    static const QStringList imageExt = {"png", "jpg", "jpeg", "bmp", "gif", "webp", "tiff"};
    static const QStringList docExt = {"note", "txt", "md", "markdown", "html", "htm"};
    bool handled = false;
    for (const QUrl &url : mime->urls()) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (imageExt.contains(suffix)) {
            const QImage img(path);
            if (!img.isNull()) {
                insertImage(img);
                handled = true;
            }
        } else if (docExt.contains(suffix)) {
            emit openFileRequested(path);     // let MainWindow open it
            handled = true;
            break;
        }
    }
    if (handled)
        event->acceptProposedAction();
    else
        event->ignore();
}

void PageDocumentItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *event)
{
    const int imgPos = imageAt(event->pos());
    if (imgPos < 0) {
        // Right-click inside a table → row/column operations.
        QTextCursor probe(m_doc);
        probe.setPosition(documentPositionAt(event->pos()));
        if (QTextTable *table = probe.currentTable()) {
            setFocus();
            m_cursor = probe;
            const QTextTableCell cell = table->cellAt(m_cursor);
            const int row = cell.row();
            const int col = cell.column();
            QMenu menu;
            QAction *ir = menu.addAction(tr("Insert Row Below"));
            QAction *ic = menu.addAction(tr("Insert Column to the Right"));
            menu.addSeparator();
            QAction *dr = menu.addAction(tr("Delete Row"));
            QAction *dc = menu.addAction(tr("Delete Column"));
            QAction *chosen = menu.exec(event->screenPos());
            if (chosen == ir)        table->insertRows(row + 1, 1);
            else if (chosen == ic)   table->insertColumns(col + 1, 1);
            else if (chosen == dr)   table->removeRows(row, 1);
            else if (chosen == dc)   table->removeColumns(col, 1);
            if (chosen) {
                recomputePages();
                emit contentsChanged();
                update();
            }
            event->accept();
            return;
        }
        event->ignore();
        return;
    }
    setFocus();
    m_selectedImagePos = imgPos;
    update();

    const WrapMode current = wrapModeOf(imgPos);
    QMenu menu;
    auto *header = menu.addAction(tr("Text wrapping"));
    header->setEnabled(false);
    menu.addSeparator();
    struct { const char *label; WrapMode mode; } items[] = {
        {QT_TR_NOOP("Inline with text"), WrapMode::Inline},
        {QT_TR_NOOP("Wrap left (text on right)"), WrapMode::FloatLeft},
        {QT_TR_NOOP("Wrap right (text on left)"), WrapMode::FloatRight},
    };
    QList<QAction *> actions;
    for (const auto &item : items) {
        auto *a = menu.addAction(tr(item.label));
        a->setCheckable(true);
        a->setChecked(current == item.mode);
        actions.append(a);
    }

    QAction *chosen = menu.exec(event->screenPos());
    for (int i = 0; i < actions.size(); ++i)
        if (chosen == actions.at(i))
            changeWrapMode(imgPos, items[i].mode);
    event->accept();
}
