#ifndef PAGESETUPDIALOG_H
#define PAGESETUPDIALOG_H

#include <QDialog>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>

class QComboBox;
class QDoubleSpinBox;

// Simple page setup: paper size, orientation and margins (in millimetres).
class PageSetupDialog : public QDialog
{
    Q_OBJECT
public:
    PageSetupDialog(QPageSize::PageSizeId paper, QPageLayout::Orientation orientation,
                    const QMarginsF &marginsMm, QWidget *parent = nullptr);

    QPageSize::PageSizeId paperSize() const;
    QPageLayout::Orientation orientation() const;
    QMarginsF marginsMm() const;

private:
    void setAllMargins(double mm);   // margin presets (full page / normal)

    QComboBox *m_paper = nullptr;
    QComboBox *m_orientation = nullptr;
    QDoubleSpinBox *m_top = nullptr;
    QDoubleSpinBox *m_bottom = nullptr;
    QDoubleSpinBox *m_left = nullptr;
    QDoubleSpinBox *m_right = nullptr;
};

#endif // PAGESETUPDIALOG_H
