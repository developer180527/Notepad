#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QColor>
#include <QMainWindow>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QString>

class PageDocumentItem;
class DocumentView;
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
    void setupToolBar();
    void setupStatusBar();
    void setupDocument();               // create + wire the DocumentView
    void syncChromeToDocument();        // toolbar/status/title follow the document
    void connectActions();
    void setupShortcutFeedback();          // flash a menu when its shortcut fires
    void flashMenu(QAction *menuAction);
    void refreshIcons();
    void applyCanvasTheme();

    // --- file ---
    void newFile();
    void openFile();
    bool saveFile();
    bool saveFileAs();
    void exportPdf();
    void printDocument();
    bool maybeSave();
    bool writeToFile(const QString &path);
    bool loadDocument(const QString &path);
    static QString usableDir(const QString &dir);   // dir if it still exists, else Documents
    bool confirmLossySave(const QString &suffix);   // warn before saving rich doc to txt/md
    void updateMarkdownActionState();
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

    // --- view ---
    void setZoom(int percent);

    // --- find / page setup ---
    void openPageSetup();

    // --- status ---
    void updateWordCount();
    void updatePageLabel();

    Ui::MainWindow *ui;

    DocumentView *m_doc = nullptr;      // the open document (one for now)
    FindBar *m_findBar = nullptr;

    FontCombo *m_fontCombo = nullptr;
    QComboBox *m_sizeCombo = nullptr;
    QComboBox *m_zoomCombo = nullptr;
    QComboBox *m_fitCombo = nullptr;
    QSlider *m_zoomSlider = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QLabel *m_pageLabel = nullptr;
    QLabel *m_wordLabel = nullptr;
    QActionGroup *m_alignGroup = nullptr;

    QColor m_lastTextColor;
    QColor m_lastHighlightColor;
    bool m_updatingControls = false;

    static constexpr int kA4WidthPx = 794;     // 210mm @ 96dpi, matches PageDocumentItem
};

#endif // MAINWINDOW_H
