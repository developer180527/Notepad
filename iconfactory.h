#ifndef ICONFACTORY_H
#define ICONFACTORY_H

// Toolbar icons from the Lucide icon set (https://lucide.dev, ISC/MIT licence).
// The SVG markup is embedded inline — no asset files — rendered with
// QSvgRenderer and stroked in a caller-supplied colour (the palette text
// colour) so the icons adapt to light/dark themes. Re-create the icons on a
// palette change to recolour them.

#include <QByteArray>
#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QString>
#include <QSvgRenderer>

namespace IconFactory {

// Render Lucide's inner markup (paths/lines on a 24x24 grid) into a tinted icon.
inline QIcon svgIcon(const QString &inner, const QColor &color, int size = 64)
{
    const QString svg = QStringLiteral(
        "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' "
        "viewBox='0 0 24 24' fill='none' stroke='%1' stroke-width='2' "
        "stroke-linecap='round' stroke-linejoin='round'>%2</svg>")
        .arg(color.name(), inner);

    QSvgRenderer renderer(svg.toUtf8());
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&p);
    p.end();
    return QIcon(pm);
}

inline QIcon newDocument(const QColor &c)
{
    return svgIcon(QStringLiteral(
        "<path d='M15 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V7Z'/>"
        "<path d='M14 2v4a2 2 0 0 0 2 2h4'/>"), c);
}

inline QIcon open(const QColor &c)
{
    return svgIcon(QStringLiteral(
        "<path d='m6 14 1.5-2.9A2 2 0 0 1 9.24 10H20a2 2 0 0 1 1.94 2.5l-1.54 6"
        "a2 2 0 0 1-1.95 1.5H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h3.9a2 2 0 0 1 1.69.9"
        "l.81 1.2a2 2 0 0 0 1.67.9H18a2 2 0 0 1 2 2v2'/>"), c);
}

inline QIcon save(const QColor &c)
{
    return svgIcon(QStringLiteral(
        "<path d='M15.2 3a2 2 0 0 1 1.4.6l3.8 3.8a2 2 0 0 1 .6 1.4V19a2 2 0 0 1-2 2"
        "H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z'/>"
        "<path d='M17 21v-7a1 1 0 0 0-1-1H8a1 1 0 0 0-1 1v7'/>"
        "<path d='M7 3v4a1 1 0 0 0 1 1h7'/>"), c);
}

inline QIcon undo(const QColor &c)
{
    return svgIcon(QStringLiteral(
        "<path d='M9 14 4 9l5-5'/>"
        "<path d='M4 9h11a5.5 5.5 0 0 1 0 11h-4'/>"), c);
}

inline QIcon redo(const QColor &c)
{
    return svgIcon(QStringLiteral(
        "<path d='m15 14 5-5-5-5'/>"
        "<path d='M20 9H9a5.5 5.5 0 0 0 0 11h4'/>"), c);
}

inline QIcon bold(const QColor &c)
{
    return svgIcon(QStringLiteral(
        "<path d='M14 12a4 4 0 0 0 0-8H6v8'/>"
        "<path d='M15 20a4 4 0 0 0 0-8H6v8Z'/>"), c);
}

inline QIcon italic(const QColor &c)
{
    return svgIcon(QStringLiteral(
        "<line x1='19' x2='10' y1='4' y2='4'/>"
        "<line x1='14' x2='5' y1='20' y2='20'/>"
        "<line x1='15' x2='9' y1='4' y2='20'/>"), c);
}

inline QIcon underline(const QColor &c)
{
    return svgIcon(QStringLiteral(
        "<path d='M6 4v6a6 6 0 0 0 12 0V4'/>"
        "<line x1='4' x2='20' y1='20' y2='20'/>"), c);
}

inline QIcon alignLeft(const QColor &c)
{
    return svgIcon(QStringLiteral(
        "<line x1='21' x2='3' y1='6' y2='6'/>"
        "<line x1='15' x2='3' y1='12' y2='12'/>"
        "<line x1='17' x2='3' y1='18' y2='18'/>"), c);
}

inline QIcon alignCenter(const QColor &c)
{
    return svgIcon(QStringLiteral(
        "<line x1='21' x2='3' y1='6' y2='6'/>"
        "<line x1='17' x2='7' y1='12' y2='12'/>"
        "<line x1='19' x2='5' y1='18' y2='18'/>"), c);
}

inline QIcon alignRight(const QColor &c)
{
    return svgIcon(QStringLiteral(
        "<line x1='21' x2='3' y1='6' y2='6'/>"
        "<line x1='21' x2='9' y1='12' y2='12'/>"
        "<line x1='21' x2='7' y1='18' y2='18'/>"), c);
}

inline QIcon alignJustify(const QColor &c)
{
    return svgIcon(QStringLiteral(
        "<line x1='3' x2='21' y1='6' y2='6'/>"
        "<line x1='3' x2='21' y1='12' y2='12'/>"
        "<line x1='3' x2='21' y1='18' y2='18'/>"), c);
}

} // namespace IconFactory

#endif // ICONFACTORY_H
