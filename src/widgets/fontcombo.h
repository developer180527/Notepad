#ifndef FONTCOMBO_H
#define FONTCOMBO_H

#include <QComboBox>
#include <QFont>

// A drop-in replacement for QFontComboBox that does not enumerate the system
// font database until the user actually opens the drop-down.
//
// QFontComboBox builds (and preview-renders) the full family list inside its
// constructor, which measured ~164 ms on this machine — by far the largest
// single cost of showing the main window. Starting with just the current family
// and filling the rest on first use makes the window appear immediately, with
// no visible difference once the list is opened.
class FontCombo : public QComboBox
{
    Q_OBJECT
public:
    explicit FontCombo(QWidget *parent = nullptr);

    QFont currentFont() const;
    void setCurrentFont(const QFont &font);

signals:
    void currentFontChanged(const QFont &font);

protected:
    void showPopup() override;

private:
    void ensurePopulated();

    bool m_populated = false;
};

#endif // FONTCOMBO_H
