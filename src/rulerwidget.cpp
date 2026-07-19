#include "rulerwidget.h"

#include "canvasview.h"
#include "pagedocumentitem.h"

#include <QFont>
#include <QLineF>
#include <QPainter>
#include <QPalette>
#include <QtMath>

RulerWidget::RulerWidget(CanvasView *view, PageDocumentItem *item, QWidget *parent)
    : QWidget(parent)
    , m_view(view)
    , m_item(item)
{
    setFixedHeight(26);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

void RulerWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const int h = height();
    const int w = width();
    const QColor base = palette().color(QPalette::Window);
    const QColor ink = palette().color(QPalette::WindowText);
    const bool dark = base.lightness() < 128;

    p.fillRect(rect(), base);
    if (!m_view || !m_item)
        return;

    const qreal sheetW = m_item->pageWidthPx();
    const qreal marginL = m_item->pageMarginLeftPx();
    const qreal marginR = m_item->pageMarginRightPx();
    auto toX = [&](qreal sceneX) { return qreal(m_view->mapFromScene(QPointF(sceneX, 0)).x()); };

    const qreal pageL = toX(0);
    const qreal pageR = toX(sheetW);
    const qreal textL = toX(marginL);
    const qreal textR = toX(sheetW - marginR);

    // Page band: margins darker, writable text area lighter.
    const QColor marginCol = dark ? base.lighter(125) : base.darker(106);
    const QColor textCol = dark ? base.lighter(160) : QColor(255, 255, 255);
    p.fillRect(QRectF(pageL, 0, pageR - pageL, h), marginCol);
    p.fillRect(QRectF(textL, 0, textR - textL, h), textCol);

    p.setPen(QPen(ink, 1));
    p.setOpacity(0.2);
    p.drawLine(0, h - 1, w, h - 1);

    QFont f = font();
    f.setPointSizeF(8.5);
    p.setFont(f);

    const qreal cm = 96.0 / 2.54;     // one centimetre in scene pixels (96 dpi)
    const int maxCm = qCeil(sheetW / cm) + 1;

    for (int i = -maxCm; i <= maxCm; ++i) {
        const qreal sceneX = marginL + i * cm;
        if (sceneX < -1 || sceneX > sheetW + 1)
            continue;
        const qreal x = toX(sceneX);

        p.setPen(QPen(ink, 1));
        p.setOpacity(0.55);
        p.drawLine(QLineF(x, h - 8, x, h - 1));
        if (i >= 1) {
            p.setOpacity(0.8);
            p.drawText(QRectF(x - 14, 1, 28, h - 9), Qt::AlignCenter, QString::number(i));
        }

        const qreal halfX = toX(sceneX + cm / 2.0);
        if (sceneX + cm / 2.0 <= sheetW) {
            p.setOpacity(0.4);
            p.drawLine(QLineF(halfX, h - 4, halfX, h - 1));
        }
    }
    p.setOpacity(1.0);
}
