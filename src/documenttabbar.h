#ifndef DOCUMENTTABBAR_H
#define DOCUMENTTABBAR_H

#include <QTabBar>

// The document tab strip, shown along the bottom of the window (just above the
// status bar). Tabs are closable and can be reordered by dragging.
//
// Phase 4 will extend this with detach/merge: dragging a tab out of the strip
// creates a new window, and dropping onto another window's strip moves the
// document there. It lives in its own class so that behaviour has a home and
// MainWindow doesn't grow mouse-handling code.
class DocumentTabBar : public QTabBar
{
    Q_OBJECT
public:
    explicit DocumentTabBar(QWidget *parent = nullptr);

    // Sets the tab label, marking unsaved documents with a leading dot.
    void setDocumentLabel(int index, const QString &name, bool modified);

signals:
    void newTabRequested();

protected:
    void mouseReleaseEvent(QMouseEvent *event) override;   // middle-click closes
    void mouseDoubleClickEvent(QMouseEvent *event) override; // double-click empty = new tab
};

#endif // DOCUMENTTABBAR_H
