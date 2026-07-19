#include "documenttabbar.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
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
    if (event->button() == Qt::LeftButton && tabAt(event->position().toPoint()) < 0) {
        emit newTabRequested();
        event->accept();
        return;
    }
    QTabBar::mouseDoubleClickEvent(event);
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
