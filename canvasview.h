#ifndef CANVASVIEW_H
#define CANVASVIEW_H

#include <QGraphicsView>
#include <QPoint>

class QGraphicsItem;

// An infinite canvas that holds a fixed-size page. Zoom is a smooth view
// transform (the page never reflows or rescales its fonts); the canvas can be
// panned with the middle mouse button or the scrollbars. Ctrl+wheel zooms
// around the cursor, a plain wheel scrolls.
class CanvasView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit CanvasView(QWidget *parent = nullptr);

    qreal zoomFactor() const { return m_zoom; }
    void setZoomFactor(qreal factor);            // absolute, 1.0 == 100%
    void setPageItem(QGraphicsItem *item) { m_pageItem = item; }

signals:
    void zoomChanged(qreal factor);

protected:
    bool viewportEvent(QEvent *event) override;      // trackpad pinch-zoom
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;

private:
    void applyIncrementalZoom(qreal factor);         // anchored under the cursor

    static constexpr qreal kMinZoom = 0.25;
    static constexpr qreal kMaxZoom = 4.0;

    qreal m_zoom = 1.0;
    bool m_panning = false;
    QPoint m_lastPanPos;
    QGraphicsItem *m_pageItem = nullptr;
};

#endif // CANVASVIEW_H
