#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QColor>
#include <QMainWindow>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QString>

class PageDocumentItem;
class CodeHighlighter;
class FontCombo;
class CanvasView;
class FindBar;
class RulerWidget;
class QGraphicsScene;
class QComboBox;
class QSlider;
class QLabel;
class QAction;
class QActionGroup;
class QFont;
class QTextCharFormat;
class QCloseEvent;
class QResizeEvent;
class QEvent;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Open a file by path (used by drag-drop, command line and macOS open events).
    void openPath(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    // --- setup ---
    void setupEditorArea();
    void setupToolBar();
    void setupStatusBar();
    void connectActions();
    void setupShortcutFeedback();          // flash a menu when its shortcut fires
    void flashMenu(QAction *menuAction);
    void refreshIcons();
    void applyCanvasTheme();
    void updateSceneRect();

    // --- file ---
    void newFile();
    void openFile();
    bool saveFile();
    bool saveFileAs();
    void exportPdf();
    void printDocument();
    bool maybeSave();
    bool writeToFile(const QString &path);
    static QString usableDir(const QString &dir);   // dir if it still exists, else Documents
    bool confirmLossySave(const QString &suffix);   // warn before saving rich doc to txt/md
    void applySyntaxMode(const QString &suffix);    // monospace + JSON/YAML highlighting
    void setMarkdownSourceMode(bool raw);           // raw .md text vs rendered document
    void updateMarkdownActionState();
    bool loadFromFile(const QString &path);
    bool writeNote(const QString &path);
    bool readNote(const QString &path);
    void setCurrentFile(const QString &path);

    // --- format ---
    void mergeFormatOnSelection(const QTextCharFormat &format);
    void changeFontSize(int delta);
    void chooseTextColor();
    void chooseHighlightColor();
    void onFontFamilyChanged(const QFont &font);
    void onFontSizeChanged(const QString &text);
    void insertImage();
    void insertTable();
    void syncFormatControls();
    void applyBaseFont();

    // --- view ---
    void setZoom(int percent);
    void applyFitMode();

    // --- find / page setup ---
    void openPageSetup();
    void applyPageSetup();

    // --- status ---
    void updateWordCount();
    void updatePageLabel();

    Ui::MainWindow *ui;

    PageDocumentItem *m_editor = nullptr;
    CanvasView *m_view = nullptr;
    QGraphicsScene *m_scene = nullptr;
    FindBar *m_findBar = nullptr;
    RulerWidget *m_ruler = nullptr;
    CodeHighlighter *m_highlighter = nullptr;

    FontCombo *m_fontCombo = nullptr;
    QComboBox *m_sizeCombo = nullptr;
    QComboBox *m_zoomCombo = nullptr;
    QComboBox *m_fitCombo = nullptr;
    QSlider *m_zoomSlider = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QLabel *m_pageLabel = nullptr;
    QLabel *m_wordLabel = nullptr;
    QActionGroup *m_alignGroup = nullptr;

    QString m_filePath;
    QColor m_lastTextColor;
    QColor m_lastHighlightColor;
    QString m_baseFontFamily;
    qreal m_baseFontSize = 12.0;
    int m_zoom = 100;
    bool m_updatingControls = false;
    bool m_mdSourceMode = false;    // View ▸ Markdown Source is active

    QPageSize::PageSizeId m_paperId = QPageSize::A4;
    QPageLayout::Orientation m_orientation = QPageLayout::Portrait;
    QMarginsF m_marginsMm{25, 25, 25, 25};

    static constexpr int kA4WidthPx = 794;     // 210mm @ 96dpi, matches PageDocumentItem
};

#endif // MAINWINDOW_H
