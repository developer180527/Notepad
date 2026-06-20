#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QString>

class PageDocumentItem;
class CanvasView;
class FindBar;
class QGraphicsScene;
class QFontComboBox;
class QComboBox;
class QSlider;
class QLabel;
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
    void refreshIcons();
    void applyCanvasTheme();
    void updateSceneRect();

    // --- file ---
    void newFile();
    void openFile();
    bool saveFile();
    bool saveFileAs();
    void exportPdf();
    bool maybeSave();
    bool writeToFile(const QString &path);
    bool loadFromFile(const QString &path);
    bool writeNote(const QString &path);
    bool readNote(const QString &path);
    void setCurrentFile(const QString &path);

    // --- format ---
    void mergeFormatOnSelection(const QTextCharFormat &format);
    void onFontFamilyChanged(const QFont &font);
    void onFontSizeChanged(const QString &text);
    void insertImage();
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

    QFontComboBox *m_fontCombo = nullptr;
    QComboBox *m_sizeCombo = nullptr;
    QComboBox *m_zoomCombo = nullptr;
    QComboBox *m_fitCombo = nullptr;
    QSlider *m_zoomSlider = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QLabel *m_pageLabel = nullptr;
    QLabel *m_wordLabel = nullptr;
    QActionGroup *m_alignGroup = nullptr;

    QString m_filePath;
    QString m_baseFontFamily;
    qreal m_baseFontSize = 12.0;
    int m_zoom = 100;
    bool m_updatingControls = false;

    QPageSize::PageSizeId m_paperId = QPageSize::A4;
    QPageLayout::Orientation m_orientation = QPageLayout::Portrait;
    QMarginsF m_marginsMm{25, 25, 25, 25};

    static constexpr int kA4WidthPx = 794;     // 210mm @ 96dpi, matches PageDocumentItem
};

#endif // MAINWINDOW_H
