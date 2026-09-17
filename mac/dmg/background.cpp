// Renders the DMG window background at 1x and 2x.
//
// The output PNGs are committed (mac/dmg/background.png, background@2x.png), so
// this only needs re-running when the design changes:
//
//   c++ -std=c++17 mac/dmg/background.cpp -o /tmp/dmgbg \
//       $(pkg-config --cflags --libs Qt6Gui)   # or the equivalent -F/-framework flags
//   QT_QPA_PLATFORM=offscreen /tmp/dmgbg fonts mac/dmg
//
// Coordinates are in points and must agree with the icon positions in
// mac/dmg/settings.py.
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

namespace {

constexpr int kWidth = 660;
constexpr int kHeight = 400;
constexpr QPointF kAppIcon(180, 210);
constexpr QPointF kAppsIcon(480, 210);

QString family(const QString &path)
{
    // From data, like the app's own FontLibrary: registering by path is not
    // reliable with every platform plugin.
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("cannot read %s", qPrintable(path));
        return QString();
    }
    const int id = QFontDatabase::addApplicationFontFromData(file.readAll());
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    return families.isEmpty() ? QString() : families.first();
}

void paint(QPainter &p, const QString &serif, const QString &sans)
{
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // Warm paper, a touch lighter at the top.
    QLinearGradient paper(0, 0, 0, kHeight);
    paper.setColorAt(0.0, QColor(0xFB, 0xF9, 0xF5));
    paper.setColorAt(1.0, QColor(0xEE, 0xEA, 0xE2));
    p.fillRect(QRectF(0, 0, kWidth, kHeight), paper);

    // Faint notepad ruling below the heading, fading out towards the edges.
    for (int y = 132; y < kHeight; y += 26) {
        QLinearGradient rule(0, 0, kWidth, 0);
        rule.setColorAt(0.0, QColor(0x5A, 0x7F, 0xA8, 0));
        rule.setColorAt(0.5, QColor(0x5A, 0x7F, 0xA8, 34));
        rule.setColorAt(1.0, QColor(0x5A, 0x7F, 0xA8, 0));
        p.setPen(QPen(QBrush(rule), 1.0));
        p.drawLine(QPointF(0, y + 0.5), QPointF(kWidth, y + 0.5));
    }
    // The red margin line of a legal pad.
    p.setPen(QPen(QColor(0xD9, 0x6B, 0x5F, 70), 1.0));
    p.drawLine(QPointF(64.5, 118), QPointF(64.5, kHeight));

    // Soft halos so the icons sit on something rather than floating on lines.
    for (const QPointF &c : {kAppIcon, kAppsIcon}) {
        QRadialGradient halo(c, 110);
        halo.setColorAt(0.0, QColor(255, 255, 255, 235));
        halo.setColorAt(0.55, QColor(255, 255, 255, 150));
        halo.setColorAt(1.0, QColor(255, 255, 255, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(c, 110, 110);
    }

    // Heading.
    QFont title(serif);
    title.setPixelSize(34);
    title.setWeight(QFont::DemiBold);
    p.setFont(title);
    p.setPen(QColor(0x2A, 0x26, 0x22));
    p.drawText(QRectF(0, 30, kWidth, 44), Qt::AlignHCenter | Qt::AlignVCenter,
               QStringLiteral("Notepad"));

    QFont sub(sans);
    sub.setPixelSize(13);
    sub.setLetterSpacing(QFont::AbsoluteSpacing, 0.2);
    p.setFont(sub);
    p.setPen(QColor(0x82, 0x7A, 0x70));
    p.drawText(QRectF(0, 74, kWidth, 22), Qt::AlignHCenter | Qt::AlignVCenter,
               QStringLiteral("Drag Notepad into Applications to install"));

    // A hand-drawn feeling arrow from the app to Applications.
    const QPointF from(kAppIcon.x() + 88, kAppIcon.y() - 6);
    const QPointF to(kAppsIcon.x() - 88, kAppsIcon.y() - 6);
    QPainterPath arc(from);
    arc.quadTo(QPointF((from.x() + to.x()) / 2, from.y() - 34), to);
    QPen stroke(QColor(0x9C, 0x8F, 0x80), 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(stroke);
    p.setBrush(Qt::NoBrush);
    p.drawPath(arc);

    // Arrowhead aligned with the curve's final direction.
    const qreal angle = arc.angleAtPercent(1.0);
    p.save();
    p.translate(to);
    p.rotate(-angle);
    QPainterPath head;
    head.moveTo(-11, -6.5);
    head.lineTo(0, 0);
    head.lineTo(-11, 6.5);
    p.drawPath(head);
    p.restore();
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const QString fonts = argc > 1 ? argv[1] : QStringLiteral("fonts");
    const QString out = argc > 2 ? argv[2] : QStringLiteral(".");
    const QString serif = family(fonts + QStringLiteral("/Lora.ttf"));
    const QString sans = family(fonts + QStringLiteral("/Inter.ttf"));

    for (int scale : {1, 2}) {
        QImage img(kWidth * scale, kHeight * scale, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.scale(scale, scale);
        paint(p, serif, sans);
        p.end();
        img.setDotsPerMeterX(qRound(72.0 * scale / 0.0254));
        img.setDotsPerMeterY(qRound(72.0 * scale / 0.0254));
        img.save(out + (scale == 1 ? QStringLiteral("/background.png")
                                   : QStringLiteral("/background@2x.png")));
    }
    return 0;
}
