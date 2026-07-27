#ifndef RULERWIDGET_H
#define RULERWIDGET_H

#include <QWidget>

class CanvasView;
class PageDocumentItem;

// A horizontal ruler shown above the canvas. It reads the page geometry from the
// document item and maps it through the view's transform, so its ticks stay
// aligned with the page as you zoom and pan. Marked in centimetres, with the
// page's text area lighter than its margins (like a word processor).
class RulerWidget : public QWidget
{
    Q_OBJECT
public:
    RulerWidget(CanvasView *view, PageDocumentItem *item, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    CanvasView *m_view = nullptr;
    PageDocumentItem *m_item = nullptr;
};

#endif // RULERWIDGET_H
