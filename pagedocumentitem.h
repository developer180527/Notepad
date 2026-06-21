#ifndef PAGEDOCUMENTITEM_H
#define PAGEDOCUMENTITEM_H

#include <QGraphicsObject>
#include <QList>
#include <QMarginsF>
#include <QSizeF>
#include <QString>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextImageFormat>

class QTimer;
class QGraphicsSceneDragDropEvent;

// The editable, paginated page surface.
//
// Data and visuals are deliberately separate: a single QTextDocument is the
// model (it owns wrapping, rich text and the undo stack), while this item is
// purely a view + controller. It lays the document out into fixed-size sheets
// (QTextDocument::setPageSize) and paints one document slice per sheet, with
// real gaps between pages so a line never straddles a page break. Editing is
// driven through a managed QTextCursor; the caret and selection are rendered by
// Qt's own layout via QAbstractTextDocumentLayout::PaintContext.
class QTextFrame;

class PageDocumentItem : public QGraphicsObject
{
    Q_OBJECT
public:
    enum class WrapMode { Inline, FloatLeft, FloatRight };

    explicit PageDocumentItem(QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    QTextDocument *document() const { return m_doc; }

    // QTextEdit-like API used by MainWindow.
    void undo();
    void redo();
    void cut();
    void copy();
    void paste();
    void selectAll();
    void mergeFormatOnSelection(const QTextCharFormat &format);
    void setAlignmentValue(Qt::Alignment alignment);
    Qt::Alignment alignmentValue() const;
    QTextCharFormat currentCharFormat() const { return m_typingFormat; }
    void setBaseFont(const QFont &font);
    void insertImage(const QImage &image);
    void documentReset();          // call after loading new content into document()

    // Render page 1 (white sheet + first-page text) to an image, for the .note
    // thumbnail/Quick Look preview. maxWidthPx is the output width.
    QImage renderPreview(int maxWidthPx) const;

    // Highlight every occurrence of text (in yellow). Empty text clears it.
    void setSearchHighlight(const QString &text, QTextDocument::FindFlags flags);

    // Find / replace helpers (used by the Find bar).
    bool find(const QString &text, QTextDocument::FindFlags flags);
    bool replaceSelection(const QString &text, const QString &with,
                          QTextDocument::FindFlags flags);
    int replaceAll(const QString &text, const QString &with,
                   QTextDocument::FindFlags flags);

    int pageCount() const { return m_pageCount; }
    int currentPage() const;
    int pageWidthPx() const { return qRound(m_sheetSize.width()); }
    qreal pageHeightPx() const { return m_sheetSize.height(); }
    qreal pageMarginLeftPx() const { return m_margins.left(); }
    qreal pageMarginRightPx() const { return m_margins.right(); }
    QRectF caretSceneRect() const;

    // Page geometry in device-independent pixels (driven by Page Setup).
    void setPageLayoutMetrics(const QSizeF &sheetPx, const QMarginsF &marginsPx, qreal gapPx);

signals:
    void cursorPositionChanged();
    void contentsChanged();
    void pageCountChanged(int count);
    void undoAvailable(bool available);
    void redoAvailable(bool available);
    void selectionAvailable(bool available);
    void ensureVisibleRequested(const QRectF &sceneRect);
    void openFileRequested(const QString &path);   // a document file was dropped

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void dragEnterEvent(QGraphicsSceneDragDropEvent *event) override;
    void dragMoveEvent(QGraphicsSceneDragDropEvent *event) override;
    void dropEvent(QGraphicsSceneDragDropEvent *event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override;

private:
    qreal textW() const { return m_sheetSize.width() - m_margins.left() - m_margins.right(); }
    qreal textH() const { return m_sheetSize.height() - m_margins.top() - m_margins.bottom(); }
    QRectF sheetRect(int page) const;
    QRectF textRect(int page) const;
    int documentPositionAt(const QPointF &itemPos) const;

    // Image selection / resize / wrap helpers.
    QTextImageFormat imageFormatAt(int pos) const;
    QRectF docRectToItem(const QRectF &docRect) const;
    QRectF imageItemRect(int imagePos) const;
    int imageAt(const QPointF &itemPos) const;        // fragment start, or -1
    QTextFrame *imageFrameAt(int pos) const;          // floating frame, or nullptr
    int imageInFrame(QTextFrame *frame) const;        // image fragment pos in a frame
    WrapMode wrapModeOf(int imagePos) const;
    void changeWrapMode(int imagePos, WrapMode mode);
    static QPointF handlePoint(const QRectF &rect, int index);  // index 3=R, 4=BR, 5=B
    int handleAt(const QPointF &itemPos) const;        // 3/4/5, or -1
    void applyImageSize(int imagePos, qreal w, qreal h);
    void clearImageSelection();

    void recomputePages();
    void recomputeSearchMatches();
    void setCursorAndNotify(const QTextCursor &cursor);
    void afterCursorMoved();        // re-sync typing format from cursor + notify
    void notifyCursorUi();

    QTextDocument *m_doc = nullptr;
    QTextCursor m_cursor;
    QTextCharFormat m_typingFormat;
    QTimer *m_blink = nullptr;

    QSizeF m_sheetSize;             // full sheet, px
    QMarginsF m_margins;            // px
    qreal m_gap = 28.0;             // px between sheets
    int m_pageCount = 1;

    bool m_caretOn = true;
    bool m_focused = false;
    bool m_selecting = false;

    // Find: highlight-all state.
    QList<QTextCursor> m_searchMatches;
    QString m_searchText;
    QTextDocument::FindFlags m_searchFlags;

    // Image selection / resize state.
    int m_selectedImagePos = -1;
    int m_resizeHandle = -1;
    QRectF m_resizeStartRect;
    qreal m_resizeAspect = 1.0;
};

#endif // PAGEDOCUMENTITEM_H
