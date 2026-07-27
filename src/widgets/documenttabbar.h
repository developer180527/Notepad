#ifndef DOCUMENTTABBAR_H
#define DOCUMENTTABBAR_H

#include <QPoint>
#include <QTabBar>

// The document tab strip, shown along the bottom of the window (just above the
// status bar). Tabs are closable and can be reordered by dragging.
//
// Dragging a tab *out* of the strip hands off to the window, which detaches the
// document into its own window; dropping a dragged tab onto another window's
// strip merges it there. The bar only detects the gestures and reports them —
// moving documents between windows is MainWindow's job.
class DocumentTabBar : public QTabBar
{
    Q_OBJECT
public:
    explicit DocumentTabBar(QWidget *parent = nullptr);

    // Mime type identifying an in-flight tab drag (same process only).
    static const char *tabMimeType();

    // Sets the tab label, marking unsaved documents with a leading dot.
    void setDocumentLabel(int index, const QString &name, bool modified);

    // Begin inline renaming of a tab (double-click, or the context menu).
    void beginRename(int index);

signals:
    void newTabRequested();
    // The user committed a new name for this tab.
    void renameRequested(int index, const QString &newName);
    // The user dragged a tab clear of the strip; the window should start a
    // detach/merge drag for this document.
    void tabDragOut(int index);
    // A tab from some window was dropped here, before `atIndex` (-1 = append).
    void tabDropped(int atIndex);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;     // middle-click closes
    void mouseDoubleClickEvent(QMouseEvent *event) override; // double-click empty = new tab
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void finishRename(bool commit);

    class QLineEdit *m_editor = nullptr;
    int m_editingIndex = -1;
    QPoint m_pressPos;
    int m_pressIndex = -1;
    bool m_dragOutEmitted = false;
};

#endif // DOCUMENTTABBAR_H
