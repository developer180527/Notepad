#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "canvasview.h"
#include "findbar.h"
#include "fontlibrary.h"
#include "iconfactory.h"
#include "pagedocumentitem.h"
#include "pagesetupdialog.h"
#include "rulerwidget.h"

#include <QActionGroup>
#include <QApplication>
#include <QBuffer>
#include <QCloseEvent>
#include <QComboBox>
#include <QDataStream>
#include <QDateTime>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QGraphicsScene>
#include <QImage>
#include <QIntValidator>
#include <QKeySequence>
#include <QLabel>
#include <QList>
#include <QMenuBar>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPair>
#include <QPalette>
#include <QPdfWriter>
#include <QPrintDialog>
#include <QPrinter>
#include <QSettings>
#include <QPixmap>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSet>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextImageFormat>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>

namespace {
constexpr quint32 kNoteVersion = 3;   // v2 adds fonts; v3 adds a preview image
const char *kNoteMagic = "PPNOTE";
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    FontLibrary::load();   // register bundled portable fonts

    // Render the menu bar inside the window (above the toolbar) on every
    // platform, instead of macOS's global menu bar — matches the design.
    ui->menubar->setNativeMenuBar(false);
    // Roomier, bolder, slightly lowered menu items. rgba hover works in both
    // light and dark themes; text colour is inherited so it adapts too.
    ui->menubar->setStyleSheet(QStringLiteral(
        "QMenuBar { padding: 6px 8px 4px 8px; font-size: 15px; font-weight: bold; }"
        "QMenuBar::item { padding: 6px 8px; margin: 0px 4px; background: transparent;"
        " border-radius: 6px; }"
        "QMenuBar::item:selected { background: rgba(128,128,128,0.28); }"
        "QMenuBar::item:pressed { background: rgba(128,128,128,0.40); }"));

    setupEditorArea();
    applyCanvasTheme();
    setupToolBar();
    setupStatusBar();
    connectActions();
    refreshIcons();
    applyPageSetup();

    // Default to a bundled (portable) font so new documents render identically
    // everywhere; fall back to the system default if no bundled font loaded.
    m_baseFontFamily = FontLibrary::defaultFamily().isEmpty()
                           ? m_editor->document()->defaultFont().family()
                           : FontLibrary::defaultFamily();
    m_baseFontSize = 12.0;
    applyBaseFont();
    m_updatingControls = true;
    m_fontCombo->setCurrentFont(QFont(m_baseFontFamily));
    m_sizeCombo->setCurrentText(QStringLiteral("12"));
    m_updatingControls = false;

    setCurrentFile(QString());
    setZoom(100);
    updateWordCount();
    updatePageLabel();

    // Restore the last window size/position.
    const QByteArray geom = QSettings().value(QStringLiteral("ui/geometry")).toByteArray();
    if (!geom.isEmpty())
        restoreGeometry(geom);

    m_editor->setFocus();
    statusBar()->showMessage(tr("Ready"), 2000);
}

MainWindow::~MainWindow()
{
    delete ui;
}

// ---------------------------------------------------------------- setup

void MainWindow::setupEditorArea()
{
    // Data and visuals are separate: one QTextDocument (inside the item) is the
    // model; the item renders it as fixed A4 sheets on the canvas. The view is a
    // QGraphicsView the user zooms (a view transform) and pans — no proxy widget.
    m_editor = new PageDocumentItem();      // owned by the scene once added

    m_scene = new QGraphicsScene(this);
    m_scene->addItem(m_editor);
    m_editor->setPos(0, 0);

    m_view = new CanvasView(this);
    m_view->setScene(m_scene);
    m_view->setFrameShape(QFrame::NoFrame);   // keep the ruler aligned with the viewport

    m_findBar = new FindBar(this);
    m_findBar->hide();

    m_ruler = new RulerWidget(m_view, m_editor, this);

    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(m_findBar);
    centralLayout->addWidget(m_ruler);
    centralLayout->addWidget(m_view, 1);
    setCentralWidget(central);

    // The ruler must repaint whenever the page moves under it (zoom or scroll).
    connect(m_view, &CanvasView::zoomChanged, this, [this](qreal) { m_ruler->update(); });
    connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int) { m_ruler->update(); });

    updateSceneRect();
}

void MainWindow::updateSceneRect()
{
    if (!m_scene || !m_editor)
        return;
    // Pad generously around the pages so they can be panned freely on the canvas.
    const qreal pad = 260.0;
    m_scene->setSceneRect(m_editor->boundingRect().adjusted(-pad, -pad, pad, pad));
}

void MainWindow::setupToolBar()
{
    QToolBar *tb = addToolBar(tr("Main Toolbar"));
    tb->setObjectName(QStringLiteral("mainToolBar"));
    tb->setMovable(false);
    tb->setFloatable(false);
    tb->setIconSize(QSize(22, 22));
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);

    tb->addAction(ui->actionNew);
    tb->addAction(ui->actionOpen);
    tb->addAction(ui->actionSave);
    tb->addSeparator();
    tb->addAction(ui->actionUndo);
    tb->addAction(ui->actionRedo);
    tb->addSeparator();

    m_fontCombo = new QFontComboBox(tb);
    m_fontCombo->setMaximumWidth(190);
    m_fontCombo->setToolTip(tr("Font family"));
    tb->addWidget(m_fontCombo);

    m_sizeCombo = new QComboBox(tb);
    m_sizeCombo->setEditable(true);
    m_sizeCombo->setInsertPolicy(QComboBox::NoInsert);
    // Numbers only — block any non-digit input in the editable field.
    m_sizeCombo->setValidator(new QIntValidator(1, 999, m_sizeCombo));
    for (int s : {8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48, 72})
        m_sizeCombo->addItem(QString::number(s));
    m_sizeCombo->setCurrentText(QStringLiteral("12"));
    m_sizeCombo->setMaximumWidth(64);
    m_sizeCombo->setToolTip(tr("Font size"));
    tb->addWidget(m_sizeCombo);
    tb->addSeparator();

    tb->addAction(ui->actionBold);
    tb->addAction(ui->actionItalic);
    tb->addAction(ui->actionUnderline);
    tb->addSeparator();

    m_alignGroup = new QActionGroup(this);
    m_alignGroup->setExclusive(true);
    for (QAction *a : {ui->actionAlignLeft, ui->actionAlignCenter,
                       ui->actionAlignRight, ui->actionAlignJustify})
        m_alignGroup->addAction(a);
    ui->actionAlignLeft->setChecked(true);
    tb->addAction(ui->actionAlignLeft);
    tb->addAction(ui->actionAlignCenter);
    tb->addAction(ui->actionAlignRight);
    tb->addAction(ui->actionAlignJustify);
    tb->addSeparator();

    auto *zoomOutBtn = new QToolButton(tb);
    zoomOutBtn->setText(QString(QChar(0x2212))); // minus sign
    zoomOutBtn->setToolTip(tr("Zoom out"));
    connect(zoomOutBtn, &QToolButton::clicked, ui->actionZoomOut, &QAction::trigger);
    tb->addWidget(zoomOutBtn);

    m_zoomCombo = new QComboBox(tb);
    m_zoomCombo->setEditable(true);
    m_zoomCombo->setInsertPolicy(QComboBox::NoInsert);
    for (int z : {50, 75, 100, 125, 150, 200, 400})
        m_zoomCombo->addItem(QString::number(z) + QStringLiteral("%"));
    m_zoomCombo->setCurrentText(QStringLiteral("100%"));
    m_zoomCombo->setMaximumWidth(82);
    m_zoomCombo->setToolTip(tr("Zoom level"));
    tb->addWidget(m_zoomCombo);

    auto *zoomInBtn = new QToolButton(tb);
    zoomInBtn->setText(QStringLiteral("+"));
    zoomInBtn->setToolTip(tr("Zoom in"));
    connect(zoomInBtn, &QToolButton::clicked, ui->actionZoomIn, &QAction::trigger);
    tb->addWidget(zoomInBtn);
    tb->addSeparator();

    m_fitCombo = new QComboBox(tb);
    m_fitCombo->addItem(tr("Fit Width"));
    m_fitCombo->addItem(tr("Actual Size"));
    m_fitCombo->setMaximumWidth(120);
    m_fitCombo->setToolTip(tr("Page fit"));
    tb->addWidget(m_fitCombo);
}

void MainWindow::setupStatusBar()
{
    m_pageLabel = new QLabel(tr("Page 1 of 1"), this);
    m_wordLabel = new QLabel(tr("Words: 0"), this);
    m_wordLabel->setContentsMargins(18, 0, 0, 0);
    statusBar()->addWidget(m_pageLabel);
    statusBar()->addWidget(m_wordLabel);

    m_zoomLabel = new QLabel(QStringLiteral("100%"), this);
    m_zoomLabel->setMinimumWidth(38);
    m_zoomLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_zoomSlider = new QSlider(Qt::Horizontal, this);
    m_zoomSlider->setRange(25, 400);
    m_zoomSlider->setValue(100);
    m_zoomSlider->setFixedWidth(150);
    m_zoomSlider->setToolTip(tr("Zoom"));

    statusBar()->addPermanentWidget(m_zoomLabel);
    statusBar()->addPermanentWidget(m_zoomSlider);
}

void MainWindow::connectActions()
{
    // File
    connect(ui->actionNew, &QAction::triggered, this, &MainWindow::newFile);
    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::openFile);
    connect(ui->actionSave, &QAction::triggered, this, &MainWindow::saveFile);
    connect(ui->actionSaveAs, &QAction::triggered, this, &MainWindow::saveFileAs);
    connect(ui->actionExportPdf, &QAction::triggered, this, &MainWindow::exportPdf);
    connect(ui->actionPrint, &QAction::triggered, this, &MainWindow::printDocument);
    connect(ui->actionQuit, &QAction::triggered, this, &QWidget::close);

    // Edit
    connect(ui->actionUndo, &QAction::triggered, m_editor, &PageDocumentItem::undo);
    connect(ui->actionRedo, &QAction::triggered, m_editor, &PageDocumentItem::redo);
    connect(ui->actionCut, &QAction::triggered, m_editor, &PageDocumentItem::cut);
    connect(ui->actionCopy, &QAction::triggered, m_editor, &PageDocumentItem::copy);
    connect(ui->actionPaste, &QAction::triggered, m_editor, &PageDocumentItem::paste);
    connect(ui->actionSelectAll, &QAction::triggered, m_editor, &PageDocumentItem::selectAll);

    connect(m_editor, &PageDocumentItem::undoAvailable, ui->actionUndo, &QAction::setEnabled);
    connect(m_editor, &PageDocumentItem::redoAvailable, ui->actionRedo, &QAction::setEnabled);
    connect(m_editor, &PageDocumentItem::selectionAvailable, ui->actionCut, &QAction::setEnabled);
    connect(m_editor, &PageDocumentItem::selectionAvailable, ui->actionCopy, &QAction::setEnabled);
    ui->actionUndo->setEnabled(false);
    ui->actionRedo->setEnabled(false);
    ui->actionCut->setEnabled(false);
    ui->actionCopy->setEnabled(false);

    // Find / replace
    connect(ui->actionFind, &QAction::triggered, this, [this] { m_findBar->activate(); });
    connect(ui->actionPageSetup, &QAction::triggered, this, &MainWindow::openPageSetup);

    auto findFlags = [](bool forward, bool cs, bool whole) {
        QTextDocument::FindFlags f;
        if (!forward) f |= QTextDocument::FindBackward;
        if (cs)       f |= QTextDocument::FindCaseSensitively;
        if (whole)    f |= QTextDocument::FindWholeWords;
        return f;
    };
    connect(m_findBar, &FindBar::findRequested, this,
            [this, findFlags](const QString &text, bool fwd, bool cs, bool whole) {
                if (!m_editor->find(text, findFlags(fwd, cs, whole)) && !text.isEmpty())
                    statusBar()->showMessage(tr("Not found: %1").arg(text), 2000);
            });
    connect(m_findBar, &FindBar::replaceRequested, this,
            [this, findFlags](const QString &text, const QString &with, bool cs, bool whole) {
                m_editor->replaceSelection(text, with, findFlags(true, cs, whole));
            });
    connect(m_findBar, &FindBar::replaceAllRequested, this,
            [this, findFlags](const QString &text, const QString &with, bool cs, bool whole) {
                const int n = m_editor->replaceAll(text, with, findFlags(true, cs, whole));
                statusBar()->showMessage(tr("Replaced %n occurrence(s)", nullptr, n), 2500);
            });
    connect(m_findBar, &FindBar::highlightRequested, this,
            [this, findFlags](const QString &text, bool cs, bool whole) {
                m_editor->setSearchHighlight(text, findFlags(true, cs, whole));
            });
    connect(m_findBar, &FindBar::closed, this, [this] {
        m_editor->setSearchHighlight(QString(), {});   // clear yellow highlights
        m_editor->setFocus();
    });

    // Insert
    connect(ui->actionInsertImage, &QAction::triggered, this, &MainWindow::insertImage);
    connect(ui->actionInsertTable, &QAction::triggered, this, &MainWindow::insertTable);

    // Format
    connect(ui->actionBold, &QAction::toggled, this, [this](bool on) {
        QTextCharFormat fmt;
        fmt.setFontWeight(on ? QFont::Bold : QFont::Normal);
        mergeFormatOnSelection(fmt);
    });
    connect(ui->actionItalic, &QAction::toggled, this, [this](bool on) {
        QTextCharFormat fmt;
        fmt.setFontItalic(on);
        mergeFormatOnSelection(fmt);
    });
    connect(ui->actionUnderline, &QAction::toggled, this, [this](bool on) {
        QTextCharFormat fmt;
        fmt.setFontUnderline(on);
        mergeFormatOnSelection(fmt);
    });
    // Ctrl/Cmd +/- change the font size (= and + both work for increase).
    ui->actionIncreaseFontSize->setShortcuts(
        {QKeySequence(QStringLiteral("Ctrl+=")), QKeySequence(QStringLiteral("Ctrl++"))});
    ui->actionDecreaseFontSize->setShortcut(QKeySequence(QStringLiteral("Ctrl+-")));
    connect(ui->actionIncreaseFontSize, &QAction::triggered, this, [this] { changeFontSize(+1); });
    connect(ui->actionDecreaseFontSize, &QAction::triggered, this, [this] { changeFontSize(-1); });

    connect(ui->actionAlignLeft, &QAction::triggered, this,
            [this] { m_editor->setAlignmentValue(Qt::AlignLeft); });
    connect(ui->actionAlignCenter, &QAction::triggered, this,
            [this] { m_editor->setAlignmentValue(Qt::AlignHCenter); });
    connect(ui->actionAlignRight, &QAction::triggered, this,
            [this] { m_editor->setAlignmentValue(Qt::AlignRight); });
    connect(ui->actionAlignJustify, &QAction::triggered, this,
            [this] { m_editor->setAlignmentValue(Qt::AlignJustify); });

    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this,
            &MainWindow::onFontFamilyChanged);
    connect(m_sizeCombo, &QComboBox::textActivated, this, &MainWindow::onFontSizeChanged);

    // View / zoom
    connect(ui->actionZoomIn, &QAction::triggered, this, [this] { setZoom(m_zoom + 10); });
    connect(ui->actionZoomOut, &QAction::triggered, this, [this] { setZoom(m_zoom - 10); });
    connect(ui->actionResetZoom, &QAction::triggered, this, [this] { setZoom(100); });
    connect(ui->actionShowRuler, &QAction::toggled, this, [this](bool on) {
        m_ruler->setVisible(on);
    });
    // Text always wraps to the fixed page width, so word-wrap toggling is moot.
    ui->actionWordWrap->setVisible(false);
    connect(m_zoomCombo, &QComboBox::textActivated, this, [this](const QString &t) {
        QString digits = t;
        digits.remove(QLatin1Char('%'));
        bool ok = false;
        const int v = digits.trimmed().toInt(&ok);
        if (ok)
            setZoom(v);
    });
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int v) {
        if (!m_updatingControls)
            setZoom(v);
    });
    connect(m_fitCombo, &QComboBox::currentIndexChanged, this, [this](int) { applyFitMode(); });
    // Wheel/Ctrl-wheel zoom from the canvas keeps the toolbar/status in sync.
    connect(m_view, &CanvasView::zoomChanged, this, [this](qreal f) {
        if (m_updatingControls)
            return;
        const int percent = qRound(f * 100.0);
        m_zoom = percent;
        m_updatingControls = true;
        m_zoomSlider->setValue(percent);
        m_zoomCombo->setCurrentText(QString::number(percent) + QStringLiteral("%"));
        m_zoomLabel->setText(QString::number(percent) + QStringLiteral("%"));
        m_updatingControls = false;
    });

    // Help
    connect(ui->actionAbout, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("About Notepad"),
                           tr("<b>Notepad</b><br>A small, fast, paginated text editor.<br><br>"
                              "Built with Qt %1.").arg(QStringLiteral(QT_VERSION_STR)));
    });
    connect(ui->actionAboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);

    // Editor state -> UI
    connect(m_editor->document(), &QTextDocument::modificationChanged, this,
            &QWidget::setWindowModified);
    connect(m_editor, &PageDocumentItem::contentsChanged, this, &MainWindow::updateWordCount);
    connect(m_editor, &PageDocumentItem::pageCountChanged, this, [this](int) {
        updateSceneRect();
        updatePageLabel();
    });
    connect(m_editor, &PageDocumentItem::cursorPositionChanged, this, [this] {
        syncFormatControls();
        updatePageLabel();
    });
    connect(m_editor, &PageDocumentItem::ensureVisibleRequested, this, [this](const QRectF &r) {
        if (m_view)
            m_view->ensureVisible(r, 24, 48);
    });
    connect(m_editor, &PageDocumentItem::openFileRequested, this, &MainWindow::openPath);
}

void MainWindow::refreshIcons()
{
    const QColor c = palette().color(QPalette::WindowText);
    ui->actionNew->setIcon(IconFactory::newDocument(c));
    ui->actionOpen->setIcon(IconFactory::open(c));
    ui->actionSave->setIcon(IconFactory::save(c));
    ui->actionUndo->setIcon(IconFactory::undo(c));
    ui->actionRedo->setIcon(IconFactory::redo(c));
    ui->actionBold->setIcon(IconFactory::bold(c));
    ui->actionItalic->setIcon(IconFactory::italic(c));
    ui->actionUnderline->setIcon(IconFactory::underline(c));
    ui->actionAlignLeft->setIcon(IconFactory::alignLeft(c));
    ui->actionAlignCenter->setIcon(IconFactory::alignCenter(c));
    ui->actionAlignRight->setIcon(IconFactory::alignRight(c));
    ui->actionAlignJustify->setIcon(IconFactory::alignJustify(c));
}

void MainWindow::applyCanvasTheme()
{
    const QColor base = palette().color(QPalette::Window);
    const QColor canvasColor =
        base.lightness() < 128 ? base.darker(118) : QColor(0xD6, 0xD6, 0xD6);
    if (m_view)
        m_view->setBackgroundBrush(canvasColor);
}

// ---------------------------------------------------------------- file ops

void MainWindow::newFile()
{
    if (!maybeSave())
        return;
    m_editor->document()->clear();
    m_editor->documentReset();
    setCurrentFile(QString());
    updateWordCount();
}

void MainWindow::openFile()
{
    if (!maybeSave())
        return;
    QSettings settings;
    const QString lastDir = settings.value(QStringLiteral("io/lastDir")).toString();
    const QString fn = QFileDialog::getOpenFileName(
        this, tr("Open"), lastDir,
        tr("All Supported (*.note *.txt *.md *.markdown *.html *.htm);;"
           "Notepad Note (*.note);;Text (*.txt);;Markdown (*.md *.markdown);;"
           "HTML (*.html *.htm);;All Files (*)"));
    if (fn.isEmpty())
        return;
    settings.setValue(QStringLiteral("io/lastDir"), QFileInfo(fn).absolutePath());
    if (loadFromFile(fn))
        setCurrentFile(fn);
}

void MainWindow::openPath(const QString &path)
{
    if (path.isEmpty() || !maybeSave())
        return;
    if (loadFromFile(path))
        setCurrentFile(path);
}

bool MainWindow::saveFile()
{
    if (m_filePath.isEmpty())
        return saveFileAs();
    return writeToFile(m_filePath);
}

bool MainWindow::saveFileAs()
{
    QSettings settings;
    // Restore the last-used directory and format/filter from the previous save.
    QString selectedFilter = settings.value(QStringLiteral("io/lastSaveFilter")).toString();
    QString suggested = m_filePath;
    if (suggested.isEmpty()) {
        const QString lastDir = settings.value(QStringLiteral("io/lastDir")).toString();
        suggested = (lastDir.isEmpty() ? QString() : lastDir + QLatin1Char('/'))
                    + QStringLiteral("Untitled.note");
    }
    QString fn = QFileDialog::getSaveFileName(
        this, tr("Save As"), suggested,
        tr("Notepad Note (*.note);;Text (*.txt);;Markdown (*.md);;HTML (*.html)"),
        &selectedFilter);
    if (fn.isEmpty())
        return false;

    // Append an extension if the user didn't type one, based on the chosen filter.
    if (QFileInfo(fn).suffix().isEmpty()) {
        if (selectedFilter.contains(QStringLiteral(".txt")))
            fn += QStringLiteral(".txt");
        else if (selectedFilter.contains(QStringLiteral(".md")))
            fn += QStringLiteral(".md");
        else if (selectedFilter.contains(QStringLiteral(".html")))
            fn += QStringLiteral(".html");
        else
            fn += QStringLiteral(".note");
    }

    settings.setValue(QStringLiteral("io/lastDir"), QFileInfo(fn).absolutePath());
    settings.setValue(QStringLiteral("io/lastSaveFilter"), selectedFilter);

    if (writeToFile(fn)) {
        setCurrentFile(fn);
        return true;
    }
    return false;
}

bool MainWindow::writeToFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("note"))
        return writeNote(path);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Notepad"),
                             tr("Cannot write %1:\n%2").arg(path, file.errorString()));
        return false;
    }
    QByteArray out;
    if (suffix == QLatin1String("md") || suffix == QLatin1String("markdown"))
        out = m_editor->document()->toMarkdown().toUtf8();
    else if (suffix == QLatin1String("html") || suffix == QLatin1String("htm"))
        out = m_editor->document()->toHtml().toUtf8();
    else
        out = m_editor->document()->toPlainText().toUtf8();
    file.write(out);
    file.close();

    m_editor->document()->setModified(false);
    setWindowModified(false);
    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(path).fileName()), 2000);
    return true;
}

bool MainWindow::loadFromFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("note"))
        return readNote(path);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Notepad"),
                             tr("Cannot read %1:\n%2").arg(path, file.errorString()));
        return false;
    }
    const QString text = QString::fromUtf8(file.readAll());
    file.close();

    if (suffix == QLatin1String("md") || suffix == QLatin1String("markdown"))
        m_editor->document()->setMarkdown(text);
    else if (suffix == QLatin1String("html") || suffix == QLatin1String("htm"))
        m_editor->document()->setHtml(text);
    else
        m_editor->document()->setPlainText(text);

    m_editor->documentReset();
    m_editor->document()->setModified(false);
    updateWordCount();
    return true;
}

bool MainWindow::writeNote(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Notepad"),
                             tr("Cannot write %1:\n%2").arg(path, file.errorString()));
        return false;
    }

    // Gather embedded image resources (by name) so they travel with the file.
    QTextDocument *doc = m_editor->document();
    QList<QPair<QString, QByteArray>> images;
    QSet<QString> seen;
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid())
                continue;
            const QTextCharFormat cf = frag.charFormat();
            if (!cf.isImageFormat())
                continue;
            const QString name = cf.toImageFormat().name();
            if (name.isEmpty() || seen.contains(name))
                continue;
            seen.insert(name);
            const QVariant res = doc->resource(QTextDocument::ImageResource, QUrl(name));
            QByteArray bytes;
            QImage img;
            if (res.canConvert<QImage>())
                img = res.value<QImage>();
            else if (res.canConvert<QPixmap>())
                img = res.value<QPixmap>().toImage();
            if (!img.isNull()) {
                QBuffer buf(&bytes);
                buf.open(QIODevice::WriteOnly);
                img.save(&buf, "PNG");
            } else if (res.canConvert<QByteArray>()) {
                bytes = res.toByteArray();
            }
            if (!bytes.isEmpty())
                images.append({name, bytes});
        }
    }

    // Embed the bundled fonts actually used so the note renders identically on
    // any machine. (System fonts can't be extracted by Qt, so those rely on the
    // recipient having them — documents default to a bundled font.)
    QList<QPair<QString, QByteArray>> fonts;
    QSet<QString> familiesSeen;
    auto considerFamily = [&](const QString &family) {
        if (family.isEmpty() || familiesSeen.contains(family))
            return;
        familiesSeen.insert(family);
        const QByteArray data = FontLibrary::fontData(family);
        if (!data.isEmpty())
            fonts.append({family, data});
    };
    considerFamily(doc->defaultFont().family());
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it)
            if (it.fragment().isValid())
                considerFamily(it.fragment().charFormat().font().family());

    // Render a preview image of page 1 so Finder/Quick Look (and the Windows/
    // Linux thumbnailers later) can show the document's contents. Stored right
    // after the version so a non-Qt parser can reach it without decoding html.
    QByteArray previewPng;
    {
        const QImage preview = m_editor->renderPreview(512);
        if (!preview.isNull()) {
            QBuffer buf(&previewPng);
            buf.open(QIODevice::WriteOnly);
            preview.save(&buf, "PNG");
        }
    }

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_6_5);
    out << QByteArray(kNoteMagic) << kNoteVersion;
    out << previewPng;
    out << m_editor->document()->toHtml();
    out << static_cast<quint32>(images.size());
    for (const auto &img : images)
        out << img.first << img.second;
    out << static_cast<quint32>(fonts.size());
    for (const auto &font : fonts)
        out << font.first << font.second;
    file.close();

    m_editor->document()->setModified(false);
    setWindowModified(false);
    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(path).fileName()), 2000);
    return true;
}

bool MainWindow::readNote(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Notepad"),
                             tr("Cannot read %1:\n%2").arg(path, file.errorString()));
        return false;
    }
    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_6_5);

    QByteArray magic;
    quint32 version = 0;
    in >> magic >> version;
    if (magic != QByteArray(kNoteMagic)) {
        QMessageBox::warning(this, tr("Notepad"),
                             tr("%1 is not a valid Notepad note.").arg(QFileInfo(path).fileName()));
        return false;
    }

    if (version >= 3) {
        QByteArray previewPng;     // regenerated on next save; not needed for editing
        in >> previewPng;
    }

    QString html;
    quint32 imageCount = 0;
    in >> html >> imageCount;
    for (quint32 i = 0; i < imageCount; ++i) {
        QString name;
        QByteArray bytes;
        in >> name >> bytes;
        QImage img;
        if (img.loadFromData(bytes))
            m_editor->document()->addResource(QTextDocument::ImageResource, QUrl(name), img);
    }

    // v2+: register embedded fonts before laying out the html.
    if (version >= 2) {
        quint32 fontCount = 0;
        in >> fontCount;
        for (quint32 i = 0; i < fontCount; ++i) {
            QString family;
            QByteArray data;
            in >> family >> data;
            FontLibrary::registerFontData(data);
        }
    }
    file.close();

    m_editor->document()->setHtml(html);
    m_editor->documentReset();
    m_editor->document()->setModified(false);
    updateWordCount();
    return true;
}

void MainWindow::setCurrentFile(const QString &path)
{
    m_filePath = path;
    m_editor->document()->setModified(false);
    setWindowModified(false);
    const QString name = path.isEmpty() ? tr("Untitled") : QFileInfo(path).fileName();
    setWindowTitle(tr("%1[*] %2 Notepad").arg(name, QString(QChar(0x2014))));
    setWindowFilePath(path);
}

bool MainWindow::maybeSave()
{
    if (!m_editor->document()->isModified())
        return true;
    const auto ret = QMessageBox::warning(
        this, tr("Notepad"),
        tr("The document has been modified.\nDo you want to save your changes?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (ret == QMessageBox::Save)
        return saveFile();
    if (ret == QMessageBox::Cancel)
        return false;
    return true;
}

void MainWindow::exportPdf()
{
    QString suggested = m_filePath.isEmpty()
                            ? QStringLiteral("Untitled.pdf")
                            : QFileInfo(m_filePath).completeBaseName() + QStringLiteral(".pdf");
    QString fn = QFileDialog::getSaveFileName(this, tr("Export as PDF"), suggested,
                                              tr("PDF (*.pdf)"));
    if (fn.isEmpty())
        return;
    if (QFileInfo(fn).suffix().isEmpty())
        fn += QStringLiteral(".pdf");

    QPdfWriter writer(fn);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);

    // Print from a clone at the un-zoomed base font so the PDF is independent of
    // the current on-screen zoom.
    QTextDocument *doc = m_editor->document()->clone(this);
    QFont f(m_baseFontFamily);
    f.setPointSizeF(m_baseFontSize);
    doc->setDefaultFont(f);
    doc->print(&writer);
    delete doc;

    statusBar()->showMessage(tr("Exported %1").arg(QFileInfo(fn).fileName()), 2500);
}

void MainWindow::printDocument()
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QPageSize::A4));
    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("Print"));
    if (dialog.exec() != QDialog::Accepted)
        return;

    // Print from a clone at the base (un-zoomed) font, like PDF export.
    QTextDocument *doc = m_editor->document()->clone(this);
    QFont f(m_baseFontFamily);
    f.setPointSizeF(m_baseFontSize);
    doc->setDefaultFont(f);
    doc->print(&printer);
    delete doc;

    statusBar()->showMessage(tr("Sent to printer"), 2500);
}

// ---------------------------------------------------------------- format

void MainWindow::mergeFormatOnSelection(const QTextCharFormat &format)
{
    if (m_updatingControls)
        return;
    m_editor->mergeFormatOnSelection(format);
    m_editor->setFocus();
}

void MainWindow::onFontFamilyChanged(const QFont &font)
{
    if (m_updatingControls)
        return;
    // Apply to the selection only (no-op without one), like B/I/U.
    QTextCharFormat fmt;
    fmt.setFontFamilies({font.family()});
    mergeFormatOnSelection(fmt);
}

void MainWindow::onFontSizeChanged(const QString &text)
{
    if (m_updatingControls)
        return;
    bool ok = false;
    const qreal pt = text.trimmed().toDouble(&ok);
    if (!ok || pt <= 0)
        return;
    QTextCharFormat fmt;
    fmt.setFontPointSize(pt);
    mergeFormatOnSelection(fmt);
}

void MainWindow::changeFontSize(int delta)
{
    qreal cur = m_editor->currentCharFormat().fontPointSize();
    if (cur <= 0)
        cur = m_baseFontSize;
    QTextCharFormat fmt;
    fmt.setFontPointSize(qBound(1.0, qRound(cur) + qreal(delta), 999.0));
    mergeFormatOnSelection(fmt);
}

void MainWindow::applyBaseFont()
{
    // Font family/size set the document's base font (whole document). Zoom is a
    // separate canvas transform, so the point size stays true.
    QFont f(m_baseFontFamily.isEmpty() ? m_editor->document()->defaultFont().family()
                                       : m_baseFontFamily);
    f.setPointSizeF(m_baseFontSize);
    m_editor->setBaseFont(f);
}

void MainWindow::insertImage()
{
    const QString fn = QFileDialog::getOpenFileName(
        this, tr("Insert Image"), QString(),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;All Files (*)"));
    if (fn.isEmpty())
        return;
    QImage img(fn);
    if (img.isNull()) {
        QMessageBox::warning(this, tr("Notepad"), tr("Could not load image %1.").arg(fn));
        return;
    }
    m_editor->insertImage(img);
    m_editor->setFocus();
}

void MainWindow::insertTable()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Insert Table"));
    auto *rows = new QSpinBox(&dialog);
    rows->setRange(1, 100);
    rows->setValue(2);
    auto *cols = new QSpinBox(&dialog);
    cols->setRange(1, 30);
    cols->setValue(2);
    auto *form = new QFormLayout;
    form->addRow(tr("Rows:"), rows);
    form->addRow(tr("Columns:"), cols);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dialog.exec() == QDialog::Accepted) {
        m_editor->insertTable(rows->value(), cols->value());
        m_editor->setFocus();
    }
}

void MainWindow::syncFormatControls()
{
    if (m_updatingControls)
        return;
    m_updatingControls = true;

    const QTextCharFormat fmt = m_editor->currentCharFormat();
    ui->actionBold->setChecked(fmt.fontWeight() >= QFont::Bold);
    ui->actionItalic->setChecked(fmt.fontItalic());
    ui->actionUnderline->setChecked(fmt.fontUnderline());

    QString family = fmt.fontFamilies().toStringList().value(0);
    if (family.isEmpty())
        family = m_editor->document()->defaultFont().family();
    m_fontCombo->setCurrentFont(QFont(family));

    const qreal pt = fmt.fontPointSize();
    m_sizeCombo->setCurrentText(
        QString::number(pt > 0 ? qRound(pt) : qRound(m_baseFontSize)));

    const Qt::Alignment al = m_editor->alignmentValue();
    if (al & Qt::AlignHCenter)
        ui->actionAlignCenter->setChecked(true);
    else if (al & Qt::AlignRight)
        ui->actionAlignRight->setChecked(true);
    else if (al & Qt::AlignJustify)
        ui->actionAlignJustify->setChecked(true);
    else
        ui->actionAlignLeft->setChecked(true);

    m_updatingControls = false;
}

// ---------------------------------------------------------------- view

void MainWindow::setZoom(int percent)
{
    percent = qBound(25, percent, 400);
    m_zoom = percent;

    // Zoom magnifies the whole canvas via a view transform; the page keeps its
    // fixed size and the document is not relaid out.
    m_updatingControls = true;
    if (m_view)
        m_view->setZoomFactor(percent / 100.0);
    m_zoomSlider->setValue(percent);
    m_zoomCombo->setCurrentText(QString::number(percent) + QStringLiteral("%"));
    m_zoomLabel->setText(QString::number(percent) + QStringLiteral("%"));
    m_updatingControls = false;
}

void MainWindow::applyFitMode()
{
    if (!m_view || !m_view->viewport())
        return;
    if (m_fitCombo && m_fitCombo->currentIndex() == 1) {
        setZoom(100); // Actual size
        return;
    }
    // Fit width: choose a zoom so the fixed-width page fills the viewport.
    const int viewportW = m_view->viewport()->width();
    const int pageW = m_editor->pageWidthPx();
    if (pageW > 0)
        setZoom(qRound((viewportW - 40) * 100.0 / pageW));
}

// ---------------------------------------------------------------- page setup

void MainWindow::applyPageSetup()
{
    const QPageSize ps(m_paperId);
    QSizeF sheetPx(ps.sizePixels(96));
    if (m_orientation == QPageLayout::Landscape)
        sheetPx.transpose();

    const qreal pxPerMm = 96.0 / 25.4;
    const QMarginsF marginsPx(m_marginsMm.left() * pxPerMm, m_marginsMm.top() * pxPerMm,
                              m_marginsMm.right() * pxPerMm, m_marginsMm.bottom() * pxPerMm);
    m_editor->setPageLayoutMetrics(sheetPx, marginsPx, 28.0);

    updateSceneRect();
    if (m_fitCombo && m_fitCombo->currentIndex() == 0)
        applyFitMode();
    updatePageLabel();
    if (m_ruler)
        m_ruler->update();
}

void MainWindow::openPageSetup()
{
    PageSetupDialog dialog(m_paperId, m_orientation, m_marginsMm, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_paperId = dialog.paperSize();
    m_orientation = dialog.orientation();
    m_marginsMm = dialog.marginsMm();
    applyPageSetup();
}

// ---------------------------------------------------------------- status

void MainWindow::updateWordCount()
{
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    const QString text = m_editor->document()->toPlainText().trimmed();
    const int words = text.isEmpty() ? 0 : text.split(ws, Qt::SkipEmptyParts).size();
    m_wordLabel->setText(tr("Words: %1").arg(words));
}

void MainWindow::updatePageLabel()
{
    m_pageLabel->setText(tr("Page %1 of %2").arg(m_editor->currentPage()).arg(m_editor->pageCount()));
}

// ---------------------------------------------------------------- events

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (maybeSave()) {
        QSettings().setValue(QStringLiteral("ui/geometry"), saveGeometry());
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    // Only re-fit on resize when in Fit-Width mode; Actual Size stays put.
    if (m_fitCombo && m_fitCombo->currentIndex() == 0)
        applyFitMode();
    if (m_ruler)
        m_ruler->update();
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    switch (event->type()) {
    case QEvent::PaletteChange:
    case QEvent::ApplicationPaletteChange:
    case QEvent::ThemeChange:
        applyCanvasTheme();
        refreshIcons();
        break;
    default:
        break;
    }
}
