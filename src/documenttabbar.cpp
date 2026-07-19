#include "documenttabbar.h"

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
    setStyleSheet(QStringLiteral(
        "QTabBar::tab { padding: 4px 10px; margin-right: 2px; max-width: 220px;"
        " border-top-left-radius: 0px; border-top-right-radius: 0px;"
        " border-bottom-left-radius: 6px; border-bottom-right-radius: 6px; }"
        "QTabBar::tab:selected { background: rgba(128,128,128,0.30); }"
        "QTabBar::tab:!selected { background: rgba(128,128,128,0.12); }"
        "QTabBar::tab:hover:!selected { background: rgba(128,128,128,0.20); }"));
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
