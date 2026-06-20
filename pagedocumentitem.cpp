#include "pagedocumentitem.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFocusEvent>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QPalette>
#include <QStyleOptionGraphicsItem>
#include <QTextBlock>
#include <QTextDocumentFragment>
#include <QTextImageFormat>
#include <QTextLayout>
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
    m_doc->setPageSize(QSizeF(textW(), textH()));
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
    if (m_cursor.hasSelection())
        m_cursor.mergeCharFormat(format);
    m_typingFormat.merge(format);
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
    QImage img = image;
    const double maxW = textW();
    if (maxW > 0 && img.width() > maxW)
        img = img.scaledToWidth(static_cast<int>(maxW), Qt::SmoothTransformation);

    const QString name = QStringLiteral("img://%1.png").arg(QDateTime::currentMSecsSinceEpoch());
    m_doc->addResource(QTextDocument::ImageResource, QUrl(name), img);

    QTextImageFormat fmt;
    fmt.setName(name);
    fmt.setWidth(img.width());
    fmt.setHeight(img.height());
    m_cursor.insertImage(fmt);
    afterCursorMoved();
}

// --------------------------------------------------------------- find / replace

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
