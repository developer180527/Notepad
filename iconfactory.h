#ifndef ICONFACTORY_H
#define ICONFACTORY_H

// Self-contained, theme-adaptive toolbar icons drawn with QPainter.
// No .qrc resources, no external image files: every icon is rendered as a
// vector in a single colour (typically the palette text colour) so it reads
// correctly in both light and dark themes. Re-create the icons whenever the
// application palette changes to recolour them.

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>

#include <cmath>

namespace IconFactory {

// Everything is drawn on a 64x64 logical canvas and handed to QIcon, which
// scales it smoothly to whatever size the toolbar requests.
inline constexpr int kCanvas = 64;

inline QPixmap beginPixmap(QPainter &p)
{
    QPixmap pm(kCanvas, kCanvas);
    pm.fill(Qt::transparent);
    p.begin(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    return pm;
}

inline QPen strokePen(const QColor &c, qreal width = 5.0)
{
    QPen pen(c);
    pen.setWidthF(width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

// A filled triangular arrowhead with its tip at `tip`, pointing in `dir`
// (a unit-ish direction), drawn at the current brush colour.
inline void arrowHead(QPainter &p, const QPointF &tip, const QPointF &dir,
                      qreal len = 13.0, qreal halfWidth = 9.0)
{
    // Normalise direction.
    const qreal mag = std::hypot(dir.x(), dir.y());
    const QPointF d = mag > 0 ? QPointF(dir.x() / mag, dir.y() / mag) : QPointF(1, 0);
    const QPointF n(-d.y(), d.x());                 // perpendicular
    const QPointF base = tip - d * len;
    QPolygonF tri;
    tri << tip << (base + n * halfWidth) << (base - n * halfWidth);
    p.drawPolygon(tri);
}

inline QIcon glyphIcon(const QColor &color, const QString &letter,
                       bool bold, bool italic, bool underline)
{
    QPainter p;
    QPixmap pm = beginPixmap(p);
    QFont f = p.font();
    f.setPixelSize(46);
    f.setBold(bold);
    f.setItalic(italic);
    f.setUnderline(underline);
    p.setFont(f);
    p.setPen(color);
    p.drawText(QRectF(0, 0, kCanvas, kCanvas), Qt::AlignCenter, letter);
    p.end();
    return QIcon(pm);
}

inline QIcon newDocument(const QColor &color)
{
    QPainter p;
    QPixmap pm = beginPixmap(p);
    p.setPen(strokePen(color));
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(16, 10);
    path.lineTo(40, 10);
    path.lineTo(48, 18);
    path.lineTo(48, 54);
    path.lineTo(16, 54);
    path.closeSubpath();
    p.drawPath(path);
    // folded corner
    p.drawLine(QPointF(40, 10), QPointF(40, 18));
    p.drawLine(QPointF(40, 18), QPointF(48, 18));
    p.end();
    return QIcon(pm);
}

inline QIcon open(const QColor &color)
{
    QPainter p;
    QPixmap pm = beginPixmap(p);
    p.setPen(strokePen(color));
    p.setBrush(Qt::NoBrush);
    // folder back with tab
    QPainterPath body;
    body.moveTo(12, 22);
    body.lineTo(26, 22);
    body.lineTo(30, 27);
    body.lineTo(52, 27);
    body.lineTo(52, 48);
    body.lineTo(12, 48);
    body.closeSubpath();
    p.drawPath(body);
    // open lid (slanted front)
    QPainterPath lid;
    lid.moveTo(12, 48);
    lid.lineTo(20, 34);
    lid.lineTo(58, 34);
    lid.lineTo(52, 48);
    p.drawPath(lid);
    p.end();
    return QIcon(pm);
}

inline QIcon save(const QColor &color)
{
    QPainter p;
    QPixmap pm = beginPixmap(p);
    p.setPen(strokePen(color, 4.5));
    p.setBrush(Qt::NoBrush);
    // floppy outline with clipped corner
    QPainterPath body;
    body.moveTo(14, 14);
    body.lineTo(44, 14);
    body.lineTo(50, 20);
    body.lineTo(50, 50);
    body.lineTo(14, 50);
    body.closeSubpath();
    p.drawPath(body);
    // label area
    p.drawRect(QRectF(22, 34, 20, 16));
    // shutter (filled)
    p.setBrush(color);
    p.drawRect(QRectF(34, 16, 8, 12));
    p.end();
    return QIcon(pm);
}

inline QIcon undo(const QColor &color)
{
    QPainter p;
    QPixmap pm = beginPixmap(p);
    p.setPen(strokePen(color));
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(44, 46);
    path.lineTo(44, 32);
    path.quadTo(44, 22, 32, 22);
    path.lineTo(24, 22);
    p.drawPath(path);
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    arrowHead(p, QPointF(16, 22), QPointF(-1, 0));
    p.end();
    return QIcon(pm);
}

inline QIcon redo(const QColor &color)
{
    QPainter p;
    QPixmap pm = beginPixmap(p);
    p.setPen(strokePen(color));
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(20, 46);
    path.lineTo(20, 32);
    path.quadTo(20, 22, 32, 22);
    path.lineTo(40, 22);
    p.drawPath(path);
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    arrowHead(p, QPointF(48, 22), QPointF(1, 0));
    p.end();
    return QIcon(pm);
}

inline QIcon bold(const QColor &color)     { return glyphIcon(color, "B", true, false, false); }
inline QIcon italic(const QColor &color)   { return glyphIcon(color, "I", false, true, false); }
inline QIcon underline(const QColor &color){ return glyphIcon(color, "U", false, false, true); }

// Alignment icons: four horizontal bars; `anchor` is 0=left, 1=center,
// 2=right, 3=justify (full width on every line).
inline QIcon align(const QColor &color, int anchor)
{
    QPainter p;
    QPixmap pm = beginPixmap(p);
    p.setPen(strokePen(color, 5.0));
    const qreal ys[4] = {18, 29, 40, 51};
    const qreal full = 40.0;
    const qreal shortLens[4] = {40, 26, 40, 24};
    for (int i = 0; i < 4; ++i) {
        qreal len = (anchor == 3) ? full : shortLens[i];
        qreal x1, x2;
        switch (anchor) {
        case 1: x1 = 32 - len / 2; x2 = 32 + len / 2; break;       // center
        case 2: x2 = 52;           x1 = 52 - len;     break;       // right
        default: x1 = 12;          x2 = 12 + len;     break;       // left / justify
        }
        p.drawLine(QPointF(x1, ys[i]), QPointF(x2, ys[i]));
    }
    p.end();
    return QIcon(pm);
}

inline QIcon alignLeft(const QColor &c)    { return align(c, 0); }
inline QIcon alignCenter(const QColor &c)  { return align(c, 1); }
inline QIcon alignRight(const QColor &c)   { return align(c, 2); }
inline QIcon alignJustify(const QColor &c) { return align(c, 3); }

} // namespace IconFactory

#endif // ICONFACTORY_H
