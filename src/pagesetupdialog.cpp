#include "pagesetupdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>

namespace {
QDoubleSpinBox *makeMarginSpin(double value)
{
    auto *spin = new QDoubleSpinBox;
    spin->setRange(0, 100);
    spin->setSuffix(QStringLiteral(" mm"));
    spin->setDecimals(1);
    spin->setSingleStep(1.0);
    spin->setValue(value);
    return spin;
}
} // namespace

PageSetupDialog::PageSetupDialog(QPageSize::PageSizeId paper,
                                 QPageLayout::Orientation orientation,
                                 const QMarginsF &marginsMm, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Page Setup"));

    m_paper = new QComboBox(this);
    m_paper->addItem(tr("A4"), QVariant::fromValue<int>(QPageSize::A4));
    m_paper->addItem(tr("Letter"), QVariant::fromValue<int>(QPageSize::Letter));
    m_paper->addItem(tr("Legal"), QVariant::fromValue<int>(QPageSize::Legal));
    m_paper->addItem(tr("A5"), QVariant::fromValue<int>(QPageSize::A5));
    for (int i = 0; i < m_paper->count(); ++i) {
        if (m_paper->itemData(i).toInt() == static_cast<int>(paper)) {
            m_paper->setCurrentIndex(i);
            break;
        }
    }

    m_orientation = new QComboBox(this);
    m_orientation->addItem(tr("Portrait"), QVariant::fromValue<int>(QPageLayout::Portrait));
    m_orientation->addItem(tr("Landscape"), QVariant::fromValue<int>(QPageLayout::Landscape));
    m_orientation->setCurrentIndex(orientation == QPageLayout::Landscape ? 1 : 0);

    auto *form = new QFormLayout;
    form->addRow(tr("Paper size:"), m_paper);
    form->addRow(tr("Orientation:"), m_orientation);

    m_top = makeMarginSpin(marginsMm.top());
    m_bottom = makeMarginSpin(marginsMm.bottom());
    m_left = makeMarginSpin(marginsMm.left());
    m_right = makeMarginSpin(marginsMm.right());

    auto *marginsBox = new QGroupBox(tr("Margins"), this);
    auto *grid = new QGridLayout(marginsBox);
    grid->addWidget(new QLabel(tr("Top")), 0, 0);
    grid->addWidget(m_top, 0, 1);
    grid->addWidget(new QLabel(tr("Bottom")), 0, 2);
    grid->addWidget(m_bottom, 0, 3);
    grid->addWidget(new QLabel(tr("Left")), 1, 0);
    grid->addWidget(m_left, 1, 1);
    grid->addWidget(new QLabel(tr("Right")), 1, 2);
    grid->addWidget(m_right, 1, 3);

    // Quick presets — "Full page" zeroes every margin so the text area expands
    // right to the edge of the sheet (the layout supports it, it was just buried
    // behind typing 0 into four boxes).
    auto *presets = new QHBoxLayout;
    auto *fullPage = new QPushButton(tr("Full page (no margins)"), this);
    auto *normal = new QPushButton(tr("Normal (25 mm)"), this);
    connect(fullPage, &QPushButton::clicked, this, [this] { setAllMargins(0.0); });
    connect(normal, &QPushButton::clicked, this, [this] { setAllMargins(25.0); });
    presets->addWidget(fullPage);
    presets->addWidget(normal);
    presets->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(marginsBox);
    layout->addLayout(presets);
    layout->addWidget(buttons);
}

void PageSetupDialog::setAllMargins(double mm)
{
    m_top->setValue(mm);
    m_bottom->setValue(mm);
    m_left->setValue(mm);
    m_right->setValue(mm);
}

QPageSize::PageSizeId PageSetupDialog::paperSize() const
{
    return static_cast<QPageSize::PageSizeId>(m_paper->currentData().toInt());
}

QPageLayout::Orientation PageSetupDialog::orientation() const
{
    return static_cast<QPageLayout::Orientation>(m_orientation->currentData().toInt());
}

QMarginsF PageSetupDialog::marginsMm() const
{
    return QMarginsF(m_left->value(), m_top->value(), m_right->value(), m_bottom->value());
}
