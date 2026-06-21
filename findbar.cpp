#include "findbar.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QToolButton>

FindBar::FindBar(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(6);

    m_find = new QLineEdit(this);
    m_find->setPlaceholderText(tr("Find"));
    m_find->setClearButtonEnabled(true);
    m_find->setMinimumWidth(160);

    auto *prev = new QToolButton(this);
    prev->setText(QStringLiteral("‹"));
    prev->setToolTip(tr("Find previous"));
    auto *next = new QToolButton(this);
    next->setText(QStringLiteral("›"));
    next->setToolTip(tr("Find next"));

    m_case = new QCheckBox(tr("Aa"), this);
    m_case->setToolTip(tr("Case sensitive"));
    m_case->setChecked(true);   // match exactly what's typed by default ("a" != "A")
    m_whole = new QCheckBox(tr("Word"), this);
    m_whole->setToolTip(tr("Whole words"));

    m_replace = new QLineEdit(this);
    m_replace->setPlaceholderText(tr("Replace with"));
    m_replace->setClearButtonEnabled(true);
    m_replace->setMinimumWidth(160);

    auto *replaceOne = new QToolButton(this);
    replaceOne->setText(tr("Replace"));
    auto *replaceAll = new QToolButton(this);
    replaceAll->setText(tr("All"));
    replaceAll->setToolTip(tr("Replace all"));

    auto *close = new QToolButton(this);
    close->setText(QStringLiteral("✕"));
    close->setToolTip(tr("Close"));

    layout->addWidget(m_find);
    layout->addWidget(prev);
    layout->addWidget(next);
    layout->addWidget(m_case);
    layout->addWidget(m_whole);
    layout->addSpacing(12);
    layout->addWidget(m_replace);
    layout->addWidget(replaceOne);
    layout->addWidget(replaceAll);
    layout->addStretch(1);
    layout->addWidget(close);

    connect(next, &QToolButton::clicked, this, [this] { emitFind(true); });
    connect(prev, &QToolButton::clicked, this, [this] { emitFind(false); });
    connect(m_find, &QLineEdit::returnPressed, this, [this] { emitFind(true); });

    // Live highlight-all as the query or options change.
    auto emitHighlight = [this] {
        emit highlightRequested(m_find->text(), caseSensitive(), wholeWords());
    };
    connect(m_find, &QLineEdit::textChanged, this, [emitHighlight](const QString &) { emitHighlight(); });
    connect(m_case, &QCheckBox::toggled, this, [emitHighlight](bool) { emitHighlight(); });
    connect(m_whole, &QCheckBox::toggled, this, [emitHighlight](bool) { emitHighlight(); });
    connect(replaceOne, &QToolButton::clicked, this, [this] {
        emit replaceRequested(m_find->text(), m_replace->text(), caseSensitive(), wholeWords());
    });
    connect(replaceAll, &QToolButton::clicked, this, [this] {
        emit replaceAllRequested(m_find->text(), m_replace->text(), caseSensitive(), wholeWords());
    });
    connect(close, &QToolButton::clicked, this, [this] {
        hide();
        emit closed();
    });
}

void FindBar::activate()
{
    show();
    m_find->setFocus();
    m_find->selectAll();
}

bool FindBar::caseSensitive() const { return m_case->isChecked(); }
bool FindBar::wholeWords() const { return m_whole->isChecked(); }

void FindBar::emitFind(bool forward)
{
    emit findRequested(m_find->text(), forward, caseSensitive(), wholeWords());
}

void FindBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        emit closed();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
