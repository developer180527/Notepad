#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

class QComboBox;

// Preferences. Changes apply immediately (and to every open window) rather than
// on OK, so the effect of a theme choice is visible while choosing it.
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private:
    QComboBox *m_appTheme = nullptr;
    QComboBox *m_pageTheme = nullptr;
};

#endif // SETTINGSDIALOG_H
