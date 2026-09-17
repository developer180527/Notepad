// PageDocumentItem — input handling: keyboard, mouse, drag-and-drop and context menus.
//
// Part of the PageDocumentItem implementation, split across several files by
// responsibility; see pagedocumentitem.h for the class definition.

#include "document/pagedocumentitem.h"

#include "document/documentview.h"
#include "util/spellchecker.h"
#include "util/speech.h"
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

// Caret movement and deletion by the platform's own conventions.
//
// Rather than inspecting modifiers by hand, every navigation key is matched
// against QKeySequence's standard keys, which Qt maps per platform. That is what
// makes ⌘← go to the start of the line and ⌥← to the previous word on a Mac,
// while Home and Ctrl+← do those jobs on Windows and Linux — including the
// Emacs-style ⌃A / ⌃E / ⌃K that Mac text fields honour.
bool PageDocumentItem::handleEditingKey(QKeyEvent *event)
{
    using SK = QKeySequence::StandardKey;
    using Op = QTextCursor::MoveOperation;

    struct Move { SK key; Op op; bool select; };
    static const Move moves[] = {
        {QKeySequence::MoveToNextChar,          QTextCursor::Right,        false},
        {QKeySequence::MoveToPreviousChar,      QTextCursor::Left,         false},
        {QKeySequence::MoveToNextWord,          QTextCursor::WordRight,    false},
        {QKeySequence::MoveToPreviousWord,      QTextCursor::WordLeft,     false},
        {QKeySequence::MoveToNextLine,          QTextCursor::Down,         false},
        {QKeySequence::MoveToPreviousLine,      QTextCursor::Up,           false},
        {QKeySequence::MoveToStartOfLine,       QTextCursor::StartOfLine,  false},
        {QKeySequence::MoveToEndOfLine,         QTextCursor::EndOfLine,    false},
        {QKeySequence::MoveToStartOfBlock,      QTextCursor::StartOfBlock, false},
        {QKeySequence::MoveToEndOfBlock,        QTextCursor::EndOfBlock,   false},
        {QKeySequence::MoveToStartOfDocument,   QTextCursor::Start,        false},
        {QKeySequence::MoveToEndOfDocument,     QTextCursor::End,          false},
        {QKeySequence::SelectNextChar,          QTextCursor::Right,        true},
        {QKeySequence::SelectPreviousChar,      QTextCursor::Left,         true},
        {QKeySequence::SelectNextWord,          QTextCursor::WordRight,    true},
        {QKeySequence::SelectPreviousWord,      QTextCursor::WordLeft,     true},
        {QKeySequence::SelectNextLine,          QTextCursor::Down,         true},
        {QKeySequence::SelectPreviousLine,      QTextCursor::Up,           true},
        {QKeySequence::SelectStartOfLine,       QTextCursor::StartOfLine,  true},
        {QKeySequence::SelectEndOfLine,         QTextCursor::EndOfLine,    true},
        {QKeySequence::SelectStartOfBlock,      QTextCursor::StartOfBlock, true},
        {QKeySequence::SelectEndOfBlock,        QTextCursor::EndOfBlock,   true},
        {QKeySequence::SelectStartOfDocument,   QTextCursor::Start,        true},
        {QKeySequence::SelectEndOfDocument,     QTextCursor::End,          true},
    };

    for (const Move &m : moves) {
        if (!event->matches(m.key))
            continue;
        const auto mode = m.select ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor;

        // With a selection, a plain ← or → collapses it to that edge instead of
        // moving one character past it — the same on every platform.
        if (!m.select && m_cursor.hasSelection()
            && (m.op == QTextCursor::Left || m.op == QTextCursor::Right)) {
            const int edge = m.op == QTextCursor::Left ? m_cursor.selectionStart()
                                                       : m_cursor.selectionEnd();
            m_cursor.setPosition(edge);
            afterCursorMoved();
            return true;
        }

#if defined(Q_OS_MACOS)
        // ⌥→ lands at the *end* of the word on a Mac, not the start of the next.
        if (m.op == QTextCursor::WordRight) {
            const int before = m_cursor.position();
            m_cursor.movePosition(QTextCursor::EndOfWord, mode);
            if (m_cursor.position() == before) {
                m_cursor.movePosition(QTextCursor::NextWord, mode);
                m_cursor.movePosition(QTextCursor::EndOfWord, mode);
            }
            afterCursorMoved();
            return true;
        }
#endif
        m_cursor.movePosition(m.op, mode);
        afterCursorMoved();
        return true;
    }

    // Page Up / Page Down (and their selecting forms).
    const bool pageDown = event->matches(QKeySequence::MoveToNextPage);
    const bool pageUp = event->matches(QKeySequence::MoveToPreviousPage);
    const bool selPageDown = event->matches(QKeySequence::SelectNextPage);
    const bool selPageUp = event->matches(QKeySequence::SelectPreviousPage);
    if (pageDown || pageUp || selPageDown || selPageUp) {
        const auto mode = (selPageDown || selPageUp) ? QTextCursor::KeepAnchor
                                                     : QTextCursor::MoveAnchor;
        const auto op = (pageDown || selPageDown) ? QTextCursor::Down : QTextCursor::Up;
        for (int i = 0; i < 20; ++i)
            m_cursor.movePosition(op, mode);
        afterCursorMoved();
        return true;
    }

    // Deletion by word or line: ⌥⌫ / Ctrl+Backspace, ⌥⌦ / Ctrl+Del, ⌃K.
    auto deleteTo = [this](QTextCursor::MoveOperation op) {
        if (m_cursor.hasSelection()) {
            m_cursor.removeSelectedText();
        } else {
            QTextCursor c = m_cursor;
            c.movePosition(op, QTextCursor::KeepAnchor);
            c.removeSelectedText();
            m_cursor = c;
        }
        afterCursorMoved();
    };
    if (event->matches(QKeySequence::DeleteStartOfWord)) { deleteTo(QTextCursor::PreviousWord); return true; }
    if (event->matches(QKeySequence::DeleteEndOfWord))   { deleteTo(QTextCursor::NextWord);     return true; }
    if (event->matches(QKeySequence::DeleteEndOfLine))   { deleteTo(QTextCursor::EndOfLine);    return true; }
    if (event->matches(QKeySequence::DeleteCompleteLine)) {
        m_cursor.movePosition(QTextCursor::StartOfLine);
        m_cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
        m_cursor.removeSelectedText();
        afterCursorMoved();
        return true;
    }
#if defined(Q_OS_MACOS)
    // ⌘⌫ deletes back to the start of the line. Qt has no standard key for it.
    if (event->key() == Qt::Key_Backspace && (event->modifiers() & Qt::ControlModifier)) {
        deleteTo(QTextCursor::StartOfLine);
        return true;
    }
#endif
    return false;
}

void PageDocumentItem::keyPressEvent(QKeyEvent *event)
{
    if (handleEditingKey(event)) {
        event->accept();
        return;
    }

    const Qt::KeyboardModifiers mods = event->modifiers();
    const bool shift = mods & Qt::ShiftModifier;

    switch (event->key()) {
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

    // Typing. A command modifier (⌘ or ⌃ on a Mac, Ctrl on Windows/Linux) means
    // "shortcut", not text — except for AltGr, which Windows reports as Ctrl+Alt
    // and which is how many keyboard layouts type @, €, [ and friends.
    bool command = mods & (Qt::ControlModifier | Qt::MetaModifier);
#if defined(Q_OS_WIN)
    if ((mods & Qt::ControlModifier) && (mods & Qt::AltModifier))
        command = false;
#endif
    if (!command && !event->text().isEmpty() && event->text().at(0).isPrint()) {
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
        // Spelling comes first: a misspelled word under the pointer is the most
        // likely reason to right-click in prose.
        if (spellingMenuFor(event))
            return;
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

// Offer corrections for a misspelled word, plus pronunciation. Returns true when
// it handled the event, so the caller can fall through to its other menus.
bool PageDocumentItem::spellingMenuFor(QGraphicsSceneContextMenuEvent *event)
{
    SpellChecker *checker = DocumentView::spellChecker();
    if (!checker->isAvailable() || !DocumentView::spellCheckEnabled())
        return false;

    int start = 0;
    int length = 0;
    const QString word = wordAt(event->pos(), &start, &length);
    if (word.isEmpty() || checker->isCorrect(word))
        return false;

    setFocus();
    QMenu menu;
    const QStringList guesses = checker->suggestions(word).mid(0, 8);
    QList<QAction *> fixes;
    if (guesses.isEmpty()) {
        QAction *none = menu.addAction(tr("No suggestions"));
        none->setEnabled(false);
    } else {
        for (const QString &g : guesses) {
            QAction *a = menu.addAction(g);
            QFont bold = a->font();
            bold.setBold(fixes.isEmpty());     // the best guess stands out
            a->setFont(bold);
            fixes << a;
        }
    }
    menu.addSeparator();
    QAction *learn = menu.addAction(tr("Add to Dictionary"));
    QAction *ignore = menu.addAction(tr("Ignore"));
    menu.addSeparator();
    QAction *speak = menu.addAction(tr("Pronounce \"%1\"").arg(word));

    QAction *chosen = menu.exec(event->screenPos());
    if (!chosen) {
        event->accept();
        return true;
    }
    if (chosen == learn) {
        checker->learn(word);
    } else if (chosen == ignore) {
        checker->ignore(word);
    } else if (chosen == speak) {
        Speech::say(word);
    } else if (fixes.contains(chosen)) {
        replaceRange(start, length, chosen->text());
    }
    // learn/ignore change the verdict for text already laid out, so re-run.
    if (chosen == learn || chosen == ignore)
        emit rehighlightRequested();
    event->accept();
    return true;
}
