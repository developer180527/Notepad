#ifndef PAGETEXTEDIT_H
#define PAGETEXTEDIT_H

#include <QTextEdit>

// The editable "page". It behaves like a single sheet of paper that grows as
// content is added: its own scrollbars are disabled and it is sized to the full
// content height so an outer QScrollArea does the scrolling. It keeps an
// accurate page count (A4 proportions) and paints faint dashed page-break lines
// where the pages divide.
class PageTextEdit : public QTextEdit
{
    Q_OBJECT
public:
    explicit PageTextEdit(QWidget *parent = nullptr);

    int pageCount() const { return m_pageCount; }
    qreal pageHeightPx() const;       // derived from the current page width
    int pageWidthPx() const { return m_pageWidth; }

    // Sets the on-screen page width in pixels (driven by Fit-Width / Actual
    // Size) and relays out, adjusting the inner margin proportionally.
    void setPageWidthPx(int width);

signals:
    void pageCountChanged(int count);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void recomputeLayout();

    static constexpr qreal kPageAspect = 297.0 / 210.0; // A4 portrait, height/width
    static constexpr int kBaseA4WidthPx = 794;          // 210mm @ 96 dpi
    int m_pageWidth = kBaseA4WidthPx;
    int m_pageCount = 1;
};

#endif // PAGETEXTEDIT_H
