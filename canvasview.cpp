#include "canvasview.h"

#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

CanvasView::CanvasView(QWidget *parent)
    : QGraphicsView(parent)
{
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing
                   | QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::NoDrag);           // leave left-drag for text selection
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setAlignment(Qt::AlignCenter);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);          // forward drops to the page item
    setOptimizationFlag(QGraphicsView::DontSavePainterState, true);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
}

void CanvasView::setZoomFactor(qreal factor)
{
    factor = qBound(kMinZoom, factor, kMaxZoom);
    if (qFuzzyCompare(factor, m_zoom))
        return;
    m_zoom = factor;
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    QTransform t;
    t.scale(m_zoom, m_zoom);
    setTransform(t);
    emit zoomChanged(m_zoom);
}

void CanvasView::applyIncrementalZoom(qreal factor)
{
    if (factor <= 0)
        return;
    const qreal target = qBound(kMinZoom, m_zoom * factor, kMaxZoom);
    if (qFuzzyCompare(target, m_zoom))
        return;
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    scale(target / m_zoom, target / m_zoom);
    m_zoom = target;
    emit zoomChanged(m_zoom);
}

void CanvasView::wheelEvent(QWheelEvent *event)
{
    // Ctrl + wheel = zoom around the cursor.
    if (event->modifiers() & Qt::ControlModifier) {
        qreal factor;
        if (event->angleDelta().y() != 0)
            factor = event->angleDelta().y() > 0 ? 1.15 : (1.0 / 1.15);
        else
            factor = 1.0 + event->pixelDelta().y() * 0.0025;
        applyIncrementalZoom(factor);
        event->accept();
        return;
    }

    // Otherwise scroll the canvas ourselves so it works consistently whether the
    // cursor is over the page or the surrounding canvas. (If we forwarded to the
    // scene, the QTextEdit proxy would swallow the event and nothing would move.)
    QPoint delta = event->pixelDelta();           // smooth trackpad scrolling
    if (delta.isNull()) {                          // classic mouse wheel
        const QPoint a = event->angleDelta();
        delta = QPoint(a.x() / 120 * 60, a.y() / 120 * 60);
    }
    if (delta.x() != 0)
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
    if (delta.y() != 0)
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
    event->accept();
}

bool CanvasView::viewportEvent(QEvent *event)
{
    // Trackpad pinch-to-zoom (macOS / Wayland native gestures).
    if (event->type() == QEvent::NativeGesture) {
        auto *gesture = static_cast<QNativeGestureEvent *>(event);
        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
            applyIncrementalZoom(1.0 + gesture->value());
            return true;
        }
    }
    return QGraphicsView::viewportEvent(event);
}

void CanvasView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPanPos = event->position().toPoint();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        const QPoint pos = event->position().toPoint();
        const QPoint delta = pos - m_lastPanPos;
        m_lastPanPos = pos;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        unsetCursor();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}
