// PageDocumentItem — the editing API used by the window (clipboard, formatting, insertion)
// and the reflow that keeps content inside the page.
//
// Part of the PageDocumentItem implementation, split across several files by
// responsibility; see pagedocumentitem.h for the class definition.

#include "document/pagedocumentitem.h"
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

// Keep every block and inline image within the page's text column.
//
// A fixed-size sheet cannot scroll sideways, so anything wider than the column
// is simply invisible: preformatted blocks (markdown ``` fences, html <pre>)
// carry nonBreakableLines so their lines never wrap, and images keep whatever
// absolute width they were given when inserted — after a page-size change that
// can be wider than the new page. Both spill past the right margin and are
// clipped away by the per-page clip in paint(). Word processors reflow such
// content to fit, which is what we do here.
void PageDocumentItem::fitContentToPageWidth()
{
    const qreal maxW = textW();
    if (maxW <= 0)
        return;

    // Collect first, edit after: changing a block or fragment format while
    // iterating invalidates the iterators.
    QList<int> unwrapBlocks;
    QList<QPair<QPair<int, int>, QTextImageFormat>> shrinkImages;

    for (QTextBlock b = m_doc->begin(); b.isValid(); b = b.next()) {
        if (b.blockFormat().nonBreakableLines())
            unwrapBlocks.append(b.position());
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid())
                continue;
            const QTextCharFormat cf = frag.charFormat();
            if (!cf.isImageFormat())
                continue;
            QTextImageFormat img = cf.toImageFormat();
            if (img.width() <= maxW || img.width() <= 0)
                continue;
            const qreal scale = maxW / img.width();
            if (img.height() > 0)
                img.setHeight(img.height() * scale);   // preserve aspect
            img.setWidth(maxW);
            shrinkImages.append({{frag.position(), frag.length()}, img});
        }
    }
    if (unwrapBlocks.isEmpty() && shrinkImages.isEmpty())
        return;

    // Reflowing is a repair, not an edit: keep the document's modified state.
    const bool wasModified = m_doc->isModified();
    QTextCursor c(m_doc);
    c.beginEditBlock();
    for (int pos : std::as_const(unwrapBlocks)) {
        c.setPosition(pos);
        QTextBlockFormat bf = c.blockFormat();
        bf.setNonBreakableLines(false);
        c.setBlockFormat(bf);
    }
    for (const auto &entry : std::as_const(shrinkImages)) {
        c.setPosition(entry.first.first);
        c.setPosition(entry.first.first + entry.first.second, QTextCursor::KeepAnchor);
        c.setCharFormat(entry.second);
    }
    c.endEditBlock();
    m_doc->setModified(wasModified);
}

// The word under a point. Returns empty when the click is not on a word.
QString PageDocumentItem::wordAt(const QPointF &itemPos, int *startOut, int *lengthOut) const
{
    const int pos = documentPositionAt(itemPos);
    if (pos < 0)
        return {};
    QTextCursor c(m_doc);
    c.setPosition(pos);
    c.select(QTextCursor::WordUnderCursor);
    const QString word = c.selectedText();
    if (word.trimmed().isEmpty())
        return {};
    if (startOut)
        *startOut = c.selectionStart();
    if (lengthOut)
        *lengthOut = c.selectionEnd() - c.selectionStart();
    return word;
}

void PageDocumentItem::replaceRange(int start, int length, const QString &with)
{
    QTextCursor c(m_doc);
    c.setPosition(start);
    c.setPosition(start + length, QTextCursor::KeepAnchor);
    c.insertText(with);
    m_cursor = c;
    afterCursorMoved();
}

void PageDocumentItem::documentReset()
{
    fitContentToPageWidth();
    // Loading a file (setPlainText/setHtml/setMarkdown) and the font/margin
    // setup around it are undoable operations — but undoing *past* a file load
    // is never what the user wants, so the freshly loaded state is the baseline.
    m_doc->clearUndoRedoStacks();
    m_cursor = QTextCursor(m_doc);
    m_typingFormat = m_cursor.charFormat();
    recomputePages();
    notifyCursorUi();
    emit ensureVisibleRequested(caretSceneRect());
    update();
}

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
    if (mime->hasHtml()) {
        m_cursor.insertHtml(mime->html());
        fitContentToPageWidth();   // pasted <pre>/wide images must still fit
    } else if (mime->hasText()) {
        m_cursor.insertText(mime->text(), m_typingFormat);
    }
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

    m_cursor.beginEditBlock();          // one undo step for the whole table
    m_cursor.insertTable(rows, columns, fmt);
    m_cursor.endEditBlock();
    afterCursorMoved();
}
