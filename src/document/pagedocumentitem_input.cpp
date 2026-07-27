// PageDocumentItem — input handling: keyboard, mouse, drag-and-drop and context menus.
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
            // Keep an existing multi-cell selection (so Cell Color applies to all
            // of them); otherwise move the caret to the clicked cell.
            if (!(m_cursor.currentTable() == table && m_cursor.hasComplexSelection()))
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
            menu.addSeparator();
            QAction *cc = menu.addAction(tr("Cell Color..."));
            QAction *cn = menu.addAction(tr("Clear Cell Color"));
            QAction *chosen = menu.exec(event->screenPos());
            if (chosen == ir)        table->insertRows(row + 1, 1);
            else if (chosen == ic)   table->insertColumns(col + 1, 1);
            else if (chosen == dr)   table->removeRows(row, 1);
            else if (chosen == dc)   table->removeColumns(col, 1);
            else if (chosen == cc || chosen == cn) {
                QColor color;
                if (chosen == cc) {
                    const QColor cur = cell.format().background().color();
                    color = QColorDialog::getColor(cur.isValid() ? cur : QColor(Qt::white),
                                                   event->widget(), tr("Cell Color"));
                    if (!color.isValid()) { event->accept(); return; }
                }
                // Apply to every selected cell, or just the clicked one.
                int firstRow = row, firstCol = col, numRows = 1, numCols = 1;
                if (m_cursor.hasComplexSelection())
                    m_cursor.selectedTableCells(&firstRow, &numRows, &firstCol, &numCols);
                for (int r = firstRow; r < firstRow + numRows; ++r) {
                    for (int c = firstCol; c < firstCol + numCols; ++c) {
                        QTextTableCell tc = table->cellAt(r, c);
                        QTextTableCellFormat f = tc.format().toTableCellFormat();
                        if (chosen == cc) f.setBackground(color);
                        else              f.clearBackground();
                        tc.setFormat(f);
                    }
                }
            }
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
