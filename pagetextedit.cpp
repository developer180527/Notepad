#include "pagetextedit.h"

#include <QAbstractTextDocumentLayout>
#include <QPaintEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextDocument>

#include <cmath>

PageTextEdit::PageTextEdit(QWidget *parent)
    : QTextEdit(parent)
{
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setLineWrapMode(QTextEdit::WidgetWidth);
    setAcceptRichText(true);
    setTabChangesFocus(false);

    // The sheet is always white with black text, independent of the app theme.
    QPalette pal = palette();
    pal.setColor(QPalette::Base, Qt::white);
    pal.setColor(QPalette::Text, Qt::black);
    setPalette(pal);

    connect(document()->documentLayout(),
            &QAbstractTextDocumentLayout::documentSizeChanged,
            this, [this](const QSizeF &) { recomputeLayout(); });

    setPageWidthPx(m_pageWidth);
}

qreal PageTextEdit::pageHeightPx() const
{
    return m_pageWidth * kPageAspect;
}

void PageTextEdit::setPageWidthPx(int width)
{
    m_pageWidth = qMax(240, width);
    document()->setDocumentMargin(qMax(28.0, m_pageWidth * 0.06));
    setFixedWidth(m_pageWidth);
    recomputeLayout();
}

void PageTextEdit::recomputeLayout()
{
    const qreal contentH = document()->documentLayout()->documentSize().height();
    const qreal ph = pageHeightPx();
    const int pages = qMax(1, static_cast<int>(std::ceil((contentH - 0.5) / ph)));

    // Round the page area up to whole pages so the final (partial) page still
    // renders as a complete white sheet.
    const int targetH = pages * static_cast<int>(std::ceil(ph));
    if (height() != targetH)
        setFixedHeight(targetH);

    if (pages != m_pageCount) {
        m_pageCount = pages;
        emit pageCountChanged(pages);
    }
    viewport()->update();
}

void PageTextEdit::paintEvent(QPaintEvent *event)
{
    QTextEdit::paintEvent(event);

    QPainter p(viewport());
    const int w = viewport()->width();
    const int h = viewport()->height();
    const qreal ph = pageHeightPx();
    const int offset = verticalScrollBar() ? verticalScrollBar()->value() : 0;

    // Faint dashed page-break lines between pages.
    QPen breakPen(QColor(200, 200, 200));
    breakPen.setStyle(Qt::DashLine);
    breakPen.setWidth(1);
    p.setPen(breakPen);
    for (int i = 1; i < m_pageCount; ++i) {
        const int y = static_cast<int>(std::round(i * ph)) - offset;
        if (y >= event->rect().top() - 1 && y <= event->rect().bottom() + 1)
            p.drawLine(0, y, w, y);
    }

    // Subtle page border so the sheet reads clearly against the grey canvas.
    QPen borderPen(QColor(208, 208, 208));
    borderPen.setWidth(1);
    p.setPen(borderPen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(0.5, 0.5, w - 1.0, h - 1.0));
}
