#include "documenttabbar.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>

DocumentTabBar::DocumentTabBar(QWidget *parent)
    : QTabBar(parent)
{
    // South-facing tabs so the strip reads as attached to the editor above it.
    setShape(QTabBar::RoundedSouth);
    setTabsClosable(true);
    setMovable(true);
    setExpanding(false);
    setDrawBase(false);
    setElideMode(Qt::ElideMiddle);
    setUsesScrollButtons(true);
    setFocusPolicy(Qt::NoFocus);
    setAcceptDrops(true);      // tabs dragged from another window can land here
    setStyleSheet(QStringLiteral(
        "QTabBar::tab { padding: 4px 10px; margin-right: 2px; max-width: 220px;"
        " border-top-left-radius: 0px; border-top-right-radius: 0px;"
        " border-bottom-left-radius: 6px; border-bottom-right-radius: 6px; }"
        "QTabBar::tab:selected { background: rgba(128,128,128,0.30); }"
        "QTabBar::tab:!selected { background: rgba(128,128,128,0.12); }"
        "QTabBar::tab:hover:!selected { background: rgba(128,128,128,0.20); }"));
}

const char *DocumentTabBar::tabMimeType()
{
    return "application/x-notepad-tab";
}

void DocumentTabBar::setDocumentLabel(int index, const QString &name, bool modified)
{
    if (index < 0 || index >= count())
        return;
    // A leading bullet is the unsaved marker — it survives eliding better than a
    // trailing asterisk, which is the first thing cut from a long file name.
    setTabText(index, modified ? QStringLiteral("• ") + name : name);
    setTabToolTip(index, name);
}

void DocumentTabBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressIndex = -1;
        m_dragOutEmitted = false;
    }
    if (event->button() == Qt::MiddleButton) {
        const int index = tabAt(event->position().toPoint());
        if (index >= 0) {
            emit tabCloseRequested(index);
            event->accept();
            return;
        }
    }
    QTabBar::mouseReleaseEvent(event);
}

void DocumentTabBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const int index = tabAt(event->position().toPoint());
        if (index < 0) {
            emit newTabRequested();
        } else {
            beginRename(index);
        }
        event->accept();
        return;
    }
    QTabBar::mouseDoubleClickEvent(event);
}

// Inline rename: a line edit laid over the tab, like a file manager. Editing in
// place keeps the tab's position and neighbours visible, which a modal dialog
// would hide.
void DocumentTabBar::beginRename(int index)
{
    if (index < 0 || index >= count() || m_editor)
        return;
    setCurrentIndex(index);
    m_editingIndex = index;

    m_editor = new QLineEdit(this);
    m_editor->setFrame(false);
    m_editor->setAlignment(Qt::AlignCenter);
    // tabToolTip holds the clean name; tabText may carry the unsaved bullet.
    m_editor->setText(tabToolTip(index));
    m_editor->setGeometry(tabRect(index).adjusted(4, 3, -4, -3));
    m_editor->show();
    m_editor->setFocus();
    // Preselect the base name so typing replaces it but the extension is easy
    // to keep — the common case is renaming "draft.md", not changing its type.
    const QString text = m_editor->text();
    const int dot = text.lastIndexOf(QLatin1Char('.'));
    if (dot > 0)
        m_editor->setSelection(0, dot);
    else
        m_editor->selectAll();

    connect(m_editor, &QLineEdit::editingFinished, this, [this] { finishRename(true); });
}

void DocumentTabBar::finishRename(bool commit)
{
    if (!m_editor)
        return;
    // Detach first: the rename may re-label or rebuild tabs, and re-entering
    // through editingFinished while tearing down would double-fire.
    QLineEdit *editor = m_editor;
    const int index = m_editingIndex;
    m_editor = nullptr;
    m_editingIndex = -1;
    const QString name = editor->text();
    editor->hide();
    editor->deleteLater();

    if (commit && index >= 0 && !name.trimmed().isEmpty())
        emit renameRequested(index, name);
}

void DocumentTabBar::keyPressEvent(QKeyEvent *event)
{
    if (m_editor && event->key() == Qt::Key_Escape) {
        finishRename(false);
        event->accept();
        return;
    }
    QTabBar::keyPressEvent(event);
}

void DocumentTabBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->position().toPoint();
        m_pressIndex = tabAt(m_pressPos);
        m_dragOutEmitted = false;
    }
    QTabBar::mousePressEvent(event);
}

void DocumentTabBar::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton) || m_pressIndex < 0 || m_dragOutEmitted) {
        QTabBar::mouseMoveEvent(event);
        return;
    }

    const QPoint pos = event->position().toPoint();
    if ((pos - m_pressPos).manhattanLength() < QApplication::startDragDistance()) {
        QTabBar::mouseMoveEvent(event);
        return;
    }

    // Inside (or close to) the strip this is an ordinary reorder — QTabBar
    // handles it. Pulling clear of the strip means "take this document out".
    // The generous margin stops a slightly sloppy horizontal drag from tearing
    // a tab off when the user only meant to reorder.
    const QRect slack = rect().adjusted(-30, -34, 30, 34);
    if (slack.contains(pos)) {
        QTabBar::mouseMoveEvent(event);
        return;
    }

    m_dragOutEmitted = true;
    const int index = m_pressIndex;
    m_pressIndex = -1;
    emit tabDragOut(index);
    event->accept();
}

void DocumentTabBar::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(tabMimeType()))
        event->acceptProposedAction();
    else
        QTabBar::dragEnterEvent(event);
}

void DocumentTabBar::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasFormat(tabMimeType()))
        event->acceptProposedAction();
    else
        QTabBar::dragMoveEvent(event);
}

void DocumentTabBar::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasFormat(tabMimeType())) {
        QTabBar::dropEvent(event);
        return;
    }
    emit tabDropped(tabAt(event->position().toPoint()));
    event->acceptProposedAction();
}
