#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QObject>

// Application appearance, persisted in QSettings and applied to every window.
//
// Two independent choices:
//  * the app theme  — chrome (menus, toolbars, dialogs): follow the OS, or force
//    light/dark. Forcing matters on platforms where the app should not track the
//    system, and for users who want a dark editor on a light desktop.
//  * the page theme — the sheet itself. A dark *page* is not the same thing as a
//    dark UI: it inverts the document's own colours, so it is deliberately kept
//    separate rather than being implied by the app theme.
class Theme : public QObject
{
    Q_OBJECT
public:
    enum class AppTheme { System, Light, Dark };
    enum class PageTheme { Paper, Dark };

    static Theme &instance();

    AppTheme appTheme() const { return m_appTheme; }
    PageTheme pageTheme() const { return m_pageTheme; }
    void setAppTheme(AppTheme theme);
    void setPageTheme(PageTheme theme);

    // True when the chrome is currently dark, whether by choice or by OS.
    bool isDarkUi() const;

    // Colours for the document sheet and the canvas behind it.
    QColor pageColor() const;
    QColor pageTextColor() const;
    QColor pageBorderColor() const;
    QColor canvasColor() const;

    void load();      // read from QSettings and apply
    void apply();     // push the app palette; emits changed()

signals:
    void changed();

private:
    Theme() = default;

    AppTheme m_appTheme = AppTheme::System;
    PageTheme m_pageTheme = PageTheme::Paper;
};

#endif // THEME_H
