#include "util/theme.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleHints>

namespace {
const char *kAppKey = "ui/appTheme";
const char *kPageKey = "ui/pageTheme";

// A neutral dark palette, used only when the user forces Dark on a system that
// is reporting light. When following the OS we leave Qt's own palette alone so
// the app matches native widgets exactly.
QPalette darkPalette()
{
    QPalette p;
    const QColor window(0x1E, 0x1E, 0x1E);
    const QColor base(0x25, 0x25, 0x25);
    const QColor text(0xE6, 0xE6, 0xE6);
    const QColor disabled(0x7F, 0x7F, 0x7F);
    const QColor highlight(0x37, 0x8A, 0xDD);

    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, window);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, window);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, highlight);
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, disabled);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabled);
    return p;
}
} // namespace

Theme &Theme::instance()
{
    static Theme theme;
    return theme;
}

void Theme::load()
{
    QSettings s;
    m_appTheme = static_cast<AppTheme>(
        s.value(QLatin1String(kAppKey), int(AppTheme::System)).toInt());
    m_pageTheme = static_cast<PageTheme>(
        s.value(QLatin1String(kPageKey), int(PageTheme::Paper)).toInt());
    apply();
}

void Theme::setAppTheme(AppTheme theme)
{
    if (m_appTheme == theme)
        return;
    m_appTheme = theme;
    QSettings().setValue(QLatin1String(kAppKey), int(theme));
    apply();
}

void Theme::setPageTheme(PageTheme theme)
{
    if (m_pageTheme == theme)
        return;
    m_pageTheme = theme;
    QSettings().setValue(QLatin1String(kPageKey), int(theme));
    emit changed();
}

bool Theme::isDarkUi() const
{
    switch (m_appTheme) {
    case AppTheme::Light: return false;
    case AppTheme::Dark:  return true;
    case AppTheme::System:
        break;
    }
    return QApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

void Theme::apply()
{
    switch (m_appTheme) {
    case AppTheme::System:
        // Hand control back to the platform: clear any forced scheme and reset
        // to the style's own palette so native colours return.
        QApplication::styleHints()->setColorScheme(Qt::ColorScheme::Unknown);
        if (QStyle *style = QApplication::style())
            QApplication::setPalette(style->standardPalette());
        break;
    case AppTheme::Light:
        QApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
        if (QStyle *style = QApplication::style())
            QApplication::setPalette(style->standardPalette());
        break;
    case AppTheme::Dark:
        QApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
        QApplication::setPalette(darkPalette());
        break;
    }
    emit changed();
}

QColor Theme::pageColor() const
{
    return m_pageTheme == PageTheme::Dark ? QColor(0x1B, 0x1B, 0x1D) : QColor(Qt::white);
}

QColor Theme::pageTextColor() const
{
    return m_pageTheme == PageTheme::Dark ? QColor(0xE4, 0xE4, 0xE6) : QColor(Qt::black);
}

QColor Theme::pageBorderColor() const
{
    return m_pageTheme == PageTheme::Dark ? QColor(0x3A, 0x3A, 0x3D) : QColor(0xD0, 0xD0, 0xD0);
}

QColor Theme::canvasColor() const
{
    // The canvas sits behind the sheet; it tracks the *page* choice so a dark
    // page never floats on a bright grey backdrop.
    if (m_pageTheme == PageTheme::Dark)
        return QColor(0x0E, 0x0E, 0x10);
    const QColor base = QApplication::palette().color(QPalette::Window);
    return base.lightness() < 128 ? base.darker(118) : QColor(0xD6, 0xD6, 0xD6);
}
