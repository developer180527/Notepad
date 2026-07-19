#include "fontcombo.h"

#include <QFontDatabase>

FontCombo::FontCombo(QWidget *parent)
    : QComboBox(parent)
{
    setEditable(false);
    // Re-broadcast text changes as font changes so callers keep the
    // QFontComboBox-style API.
    connect(this, &QComboBox::currentTextChanged, this, [this](const QString &family) {
        if (!family.isEmpty())
            emit currentFontChanged(QFont(family));
    });
}

QFont FontCombo::currentFont() const
{
    return QFont(currentText());
}

void FontCombo::setCurrentFont(const QFont &font)
{
    const QString family = font.family();
    if (family.isEmpty())
        return;
    if (findText(family) < 0)
        addItem(family);        // keep the active family selectable pre-population
    setCurrentText(family);
}

void FontCombo::showPopup()
{
    ensurePopulated();
    QComboBox::showPopup();
}

void FontCombo::ensurePopulated()
{
    if (m_populated)
        return;
    m_populated = true;

    const QString current = currentText();
    // Repopulating would otherwise fire currentTextChanged and reformat the
    // document out from under the user.
    const QSignalBlocker block(this);
    clear();
    addItems(QFontDatabase::families());
    int idx = findText(current);
    // The active family may not be in the database (e.g. a bundled font that
    // failed to register). Re-add it rather than silently snapping the user's
    // selection to whatever sorts first — setCurrentText can't rescue a
    // non-editable combo once the item is gone.
    if (idx < 0 && !current.isEmpty()) {
        insertItem(0, current);
        idx = 0;
    }
    if (idx >= 0)
        setCurrentIndex(idx);
}
