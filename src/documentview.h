#ifndef DOCUMENTVIEW_H
#define DOCUMENTVIEW_H

#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QString>
#include <QWidget>

class PageDocumentItem;
class CanvasView;
class RulerWidget;
class CodeHighlighter;
class QGraphicsScene;
class QTextDocument;

// One open document: the editor item, its scene/canvas/ruler, and every piece of
// state that belongs to *the document* rather than to the window showing it —
// file path, base font, zoom, page setup, Markdown view mode.
//
// Splitting this out is what makes multiple documents per window (and dragging a
// tab into another window) possible: a DocumentView can simply be reparented
// into a different window, carrying its undo history, cursor and zoom with it,
// with no reload and no serialisation.
//
// File I/O lives here too, but reports failures through *errorOut rather than
// putting up dialogs, so the owning window decides how to present problems.
class DocumentView : public QWidget
{
    Q_OBJECT
public:
    explicit DocumentView(QWidget *parent = nullptr);

    PageDocumentItem *editor() const { return m_editor; }
    CanvasView *canvas() const { return m_view; }
    RulerWidget *ruler() const { return m_ruler; }
    QTextDocument *document() const;

    // --- identity ---
    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &path);
    QString displayName() const;          // file name, or "Untitled"
    bool isModified() const;
    bool isMarkdownFile() const;

    // --- file i/o (no dialogs; failures come back in *errorOut) ---
    bool load(const QString &path, QString *errorOut);
    bool save(const QString &path, QString *errorOut);
    bool hasRichFormatting() const;       // would txt/md lose anything?

    // --- fonts ---
    QString baseFontFamily() const { return m_baseFontFamily; }
    qreal baseFontSize() const { return m_baseFontSize; }
    void setBaseFontFamily(const QString &family);
    void setBaseFontSize(qreal pt);
    void applyBaseFont();

    // --- zoom / fit ---
    int zoom() const { return m_zoom; }
    void setZoom(int percent);
    int fitMode() const { return m_fitMode; }     // 0 = fit width, 1 = actual size
    void setFitMode(int mode);
    void applyFitMode();

    // --- page setup ---
    QPageSize::PageSizeId paper() const { return m_paperId; }
    QPageLayout::Orientation orientation() const { return m_orientation; }
    QMarginsF marginsMm() const { return m_marginsMm; }
    void setPageSetup(QPageSize::PageSizeId paper, QPageLayout::Orientation orientation,
                      const QMarginsF &marginsMm);
    void applyPageSetup();

    // --- view modes ---
    bool markdownSourceMode() const { return m_mdSourceMode; }
    void setMarkdownSourceMode(bool raw);
    void applySyntaxMode(const QString &suffix);   // monospace + JSON/YAML colouring
    void setRulerVisible(bool visible);

    void updateSceneRect();

signals:
    void modifiedChanged(bool modified);
    void filePathChanged(const QString &path);
    void zoomChanged(int percent);
    void statusMessage(const QString &text, int timeoutMs);
    void openFileRequested(const QString &path);   // a document file was dropped

private:
    bool loadNote(const QString &path, QString *errorOut);
    bool saveNote(const QString &path, QString *errorOut);

    PageDocumentItem *m_editor = nullptr;
    QGraphicsScene *m_scene = nullptr;
    CanvasView *m_view = nullptr;
    RulerWidget *m_ruler = nullptr;
    CodeHighlighter *m_highlighter = nullptr;

    QString m_filePath;
    QString m_baseFontFamily;
    qreal m_baseFontSize = 12.0;
    int m_zoom = 100;
    int m_fitMode = 0;
    bool m_mdSourceMode = false;

    QPageSize::PageSizeId m_paperId = QPageSize::A4;
    QPageLayout::Orientation m_orientation = QPageLayout::Portrait;
    QMarginsF m_marginsMm{25, 25, 25, 25};
};

#endif // DOCUMENTVIEW_H
