#include "widgets/settingsdialog.h"

#include "util/theme.h"

#include "document/documentview.h"
#include "util/spellchecker.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings"));

    Theme &theme = Theme::instance();

    m_appTheme = new QComboBox(this);
    m_appTheme->addItem(tr("Follow system"), int(Theme::AppTheme::System));
    m_appTheme->addItem(tr("Light"), int(Theme::AppTheme::Light));
    m_appTheme->addItem(tr("Dark"), int(Theme::AppTheme::Dark));
    m_appTheme->setCurrentIndex(m_appTheme->findData(int(theme.appTheme())));

    m_pageTheme = new QComboBox(this);
    m_pageTheme->addItem(tr("Paper (white)"), int(Theme::PageTheme::Paper));
    m_pageTheme->addItem(tr("Dark"), int(Theme::PageTheme::Dark));
    m_pageTheme->setCurrentIndex(m_pageTheme->findData(int(theme.pageTheme())));

    auto *hint = new QLabel(
        tr("The page theme changes how the sheet is drawn on screen. It does not "
           "change the document itself — printing and export stay on white paper."),
        this);
    hint->setWordWrap(true);
    hint->setMinimumWidth(380);
    QFont hintFont = hint->font();
    hintFont.setPointSizeF(hintFont.pointSizeF() - 1);
    hint->setFont(hintFont);

    m_spellCheck = new QCheckBox(tr("Check spelling as I type"), this);
    m_spellCheck->setChecked(DocumentView::spellCheckEnabled());
    // Nothing to offer when the platform has no dictionary; say so rather than
    // presenting a switch that does nothing.
    if (!DocumentView::spellChecker()->isAvailable()) {
        m_spellCheck->setEnabled(false);
        m_spellCheck->setChecked(false);
        m_spellCheck->setToolTip(tr("No system dictionary is available on this computer."));
    }

    auto *form = new QFormLayout;
    form->addRow(tr("App theme:"), m_appTheme);
    form->addRow(tr("Page:"), m_pageTheme);
    form->addRow(tr("Spelling:"), m_spellCheck);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addWidget(buttons);

    // Live preview: apply on change rather than on close.
    connect(m_appTheme, &QComboBox::currentIndexChanged, this, [this](int i) {
        Theme::instance().setAppTheme(
            static_cast<Theme::AppTheme>(m_appTheme->itemData(i).toInt()));
    });
    connect(m_pageTheme, &QComboBox::currentIndexChanged, this, [this](int i) {
        Theme::instance().setPageTheme(
            static_cast<Theme::PageTheme>(m_pageTheme->itemData(i).toInt()));
    });
    connect(m_spellCheck, &QCheckBox::toggled, this, [](bool on) {
        DocumentView::setSpellCheckEnabled(on);
        emit Theme::instance().changed();   // every open document re-reads it
    });
}
