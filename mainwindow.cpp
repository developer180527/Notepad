#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "canvasview.h"
#include "iconfactory.h"
#include "pagetextedit.h"

#include <QActionGroup>
#include <QApplication>
#include <QBuffer>
#include <QCloseEvent>
#include <QComboBox>
#include <QDataStream>
#include <QDateTime>
#include <QColor>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QImage>
#include <QLabel>
#include <QList>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPair>
#include <QPalette>
#include <QPdfWriter>
#include <QPixmap>
#include <QRegularExpression>
#include <QSet>
#include <QSlider>
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

#include <cmath>

namespace {
constexpr quint32 kNoteVersion = 1;
const char *kNoteMagic = "PPNOTE";
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    setupEditorArea();
    applyCanvasTheme();
    setupToolBar();
    setupStatusBar();
    connectActions();
    refreshIcons();

    // Base font from the system default; size combo + zoom layer on top of it.
    m_baseFontFamily = m_editor->document()->defaultFont().family();
    m_baseFontSize = 12.0;
    m_updatingControls = true;
    m_fontCombo->setCurrentFont(m_editor->document()->defaultFont());
    m_sizeCombo->setCurrentText(QStringLiteral("12"));
    m_updatingControls = false;

    setCurrentFile(QString());
    setZoom(100);
    updateWordCount();
    updatePageLabel();
    m_pageProxy->setFocus();
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
    // The page is a fixed-size sheet living in a scene; the view is the canvas
    // the user zooms (a smooth view transform) and pans. No graphics effect on
    // the editor — that was the source of the scrolling glitches/memory growth.
    m_editor = new PageTextEdit;            // owned by the proxy/scene
    m_editor->setPageWidthPx(kA4WidthPx);   // fixed A4 width, never tracks the window

    m_scene = new QGraphicsScene(this);
    m_pageProxy = m_scene->addWidget(m_editor);
    m_pageProxy->setPos(0, 0);

    m_view = new CanvasView(this);
    m_view->setScene(m_scene);
    m_view->setPageItem(m_pageProxy);
    setCentralWidget(m_view);

    updateSceneRect();
}

void MainWindow::updateSceneRect()
{
    if (!m_scene || !m_pageProxy)
        return;
    // Pad generously around the page so it can be panned freely on the canvas.
    const qreal pad = 260.0;
    m_scene->setSceneRect(m_pageProxy->geometry().adjusted(-pad, -pad, pad, pad));
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
    connect(ui->actionQuit, &QAction::triggered, this, &QWidget::close);

    // Edit
    connect(ui->actionUndo, &QAction::triggered, m_editor, &QTextEdit::undo);
    connect(ui->actionRedo, &QAction::triggered, m_editor, &QTextEdit::redo);
    connect(ui->actionCut, &QAction::triggered, m_editor, &QTextEdit::cut);
    connect(ui->actionCopy, &QAction::triggered, m_editor, &QTextEdit::copy);
    connect(ui->actionPaste, &QAction::triggered, m_editor, &QTextEdit::paste);
    connect(ui->actionSelectAll, &QAction::triggered, m_editor, &QTextEdit::selectAll);

    connect(m_editor, &QTextEdit::undoAvailable, ui->actionUndo, &QAction::setEnabled);
    connect(m_editor, &QTextEdit::redoAvailable, ui->actionRedo, &QAction::setEnabled);
    connect(m_editor, &QTextEdit::copyAvailable, ui->actionCut, &QAction::setEnabled);
    connect(m_editor, &QTextEdit::copyAvailable, ui->actionCopy, &QAction::setEnabled);
    ui->actionUndo->setEnabled(false);
    ui->actionRedo->setEnabled(false);
    ui->actionCut->setEnabled(false);
    ui->actionCopy->setEnabled(false);

    // Insert
    connect(ui->actionInsertImage, &QAction::triggered, this, &MainWindow::insertImage);

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
    connect(ui->actionAlignLeft, &QAction::triggered, this,
            [this] { m_editor->setAlignment(Qt::AlignLeft); });
    connect(ui->actionAlignCenter, &QAction::triggered, this,
            [this] { m_editor->setAlignment(Qt::AlignHCenter); });
    connect(ui->actionAlignRight, &QAction::triggered, this,
            [this] { m_editor->setAlignment(Qt::AlignRight); });
    connect(ui->actionAlignJustify, &QAction::triggered, this,
            [this] { m_editor->setAlignment(Qt::AlignJustify); });

    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this,
            &MainWindow::onFontFamilyChanged);
    connect(m_sizeCombo, &QComboBox::textActivated, this, &MainWindow::onFontSizeChanged);

    // View / zoom
    connect(ui->actionZoomIn, &QAction::triggered, this, [this] { setZoom(m_zoom + 10); });
    connect(ui->actionZoomOut, &QAction::triggered, this, [this] { setZoom(m_zoom - 10); });
    connect(ui->actionResetZoom, &QAction::triggered, this, [this] { setZoom(100); });
    connect(ui->actionWordWrap, &QAction::toggled, this, [this](bool on) {
        m_editor->setLineWrapMode(on ? QTextEdit::WidgetWidth : QTextEdit::NoWrap);
        m_editor->setHorizontalScrollBarPolicy(on ? Qt::ScrollBarAlwaysOff
                                                  : Qt::ScrollBarAsNeeded);
    });
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
        QMessageBox::about(this, tr("About PagePad"),
                           tr("<b>PagePad</b><br>A small, fast, paginated text editor.<br><br>"
                              "Built with Qt %1.").arg(QStringLiteral(QT_VERSION_STR)));
    });
    connect(ui->actionAboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);

    // Editor state -> UI
    connect(m_editor->document(), &QTextDocument::modificationChanged, this,
            &QWidget::setWindowModified);
    connect(m_editor, &QTextEdit::textChanged, this, &MainWindow::updateWordCount);
    connect(m_editor, &PageTextEdit::pageCountChanged, this, [this](int) {
        updateSceneRect();
        updatePageLabel();
    });
    connect(m_editor, &QTextEdit::currentCharFormatChanged, this,
            [this](const QTextCharFormat &) { syncFormatControls(); });
    connect(m_editor, &QTextEdit::cursorPositionChanged, this, [this] {
        syncFormatControls();
        updatePageLabel();
        ensureCursorVisibleInScroll();
    });
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
    m_editor->clear();
    setCurrentFile(QString());
    updateWordCount();
}

void MainWindow::openFile()
{
    if (!maybeSave())
        return;
    const QString fn = QFileDialog::getOpenFileName(
        this, tr("Open"), QString(),
        tr("All Supported (*.note *.txt *.md *.markdown *.html *.htm);;"
           "PagePad Note (*.note);;Text (*.txt);;Markdown (*.md *.markdown);;"
           "HTML (*.html *.htm);;All Files (*)"));
    if (fn.isEmpty())
        return;
    if (loadFromFile(fn))
        setCurrentFile(fn);
}

bool MainWindow::saveFile()
{
    if (m_filePath.isEmpty())
        return saveFileAs();
    return writeToFile(m_filePath);
}

bool MainWindow::saveFileAs()
{
    QString selectedFilter;
    QString suggested = m_filePath.isEmpty() ? QStringLiteral("Untitled.note") : m_filePath;
    QString fn = QFileDialog::getSaveFileName(
        this, tr("Save As"), suggested,
        tr("PagePad Note (*.note);;Text (*.txt);;Markdown (*.md);;HTML (*.html)"),
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
        QMessageBox::warning(this, tr("PagePad"),
                             tr("Cannot write %1:\n%2").arg(path, file.errorString()));
        return false;
    }
    QByteArray out;
    if (suffix == QLatin1String("md") || suffix == QLatin1String("markdown"))
        out = m_editor->document()->toMarkdown().toUtf8();
    else if (suffix == QLatin1String("html") || suffix == QLatin1String("htm"))
        out = m_editor->toHtml().toUtf8();
    else
        out = m_editor->toPlainText().toUtf8();
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
        QMessageBox::warning(this, tr("PagePad"),
                             tr("Cannot read %1:\n%2").arg(path, file.errorString()));
        return false;
    }
    const QString text = QString::fromUtf8(file.readAll());
    file.close();

    if (suffix == QLatin1String("md") || suffix == QLatin1String("markdown"))
        m_editor->document()->setMarkdown(text);
    else if (suffix == QLatin1String("html") || suffix == QLatin1String("htm"))
        m_editor->setHtml(text);
    else
        m_editor->setPlainText(text);

    m_editor->document()->setModified(false);
    updateWordCount();
    return true;
}

bool MainWindow::writeNote(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("PagePad"),
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

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_6_5);
    out << QByteArray(kNoteMagic) << kNoteVersion;
    out << m_editor->toHtml();
    out << static_cast<quint32>(images.size());
    for (const auto &img : images)
        out << img.first << img.second;
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
        QMessageBox::warning(this, tr("PagePad"),
                             tr("Cannot read %1:\n%2").arg(path, file.errorString()));
        return false;
    }
    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_6_5);

    QByteArray magic;
    quint32 version = 0;
    in >> magic >> version;
    if (magic != QByteArray(kNoteMagic)) {
        QMessageBox::warning(this, tr("PagePad"),
                             tr("%1 is not a valid PagePad note.").arg(QFileInfo(path).fileName()));
        return false;
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
    file.close();

    m_editor->setHtml(html);
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
    setWindowTitle(tr("%1[*] %2 PagePad").arg(name, QString(QChar(0x2014))));
    setWindowFilePath(path);
}

bool MainWindow::maybeSave()
{
    if (!m_editor->document()->isModified())
        return true;
    const auto ret = QMessageBox::warning(
        this, tr("PagePad"),
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

// ---------------------------------------------------------------- format

void MainWindow::mergeFormatOnSelection(const QTextCharFormat &format)
{
    if (m_updatingControls)
        return;
    QTextCursor cursor = m_editor->textCursor();
    if (cursor.hasSelection())
        cursor.mergeCharFormat(format);
    m_editor->mergeCurrentCharFormat(format);
    m_editor->setFocus();
}

void MainWindow::onFontFamilyChanged(const QFont &font)
{
    if (m_updatingControls)
        return;
    QTextCursor cursor = m_editor->textCursor();
    if (cursor.hasSelection()) {
        QTextCharFormat fmt;
        fmt.setFontFamilies({font.family()});
        mergeFormatOnSelection(fmt);
    } else {
        m_baseFontFamily = font.family();
        applyBaseFont();
    }
    m_editor->setFocus();
}

void MainWindow::onFontSizeChanged(const QString &text)
{
    if (m_updatingControls)
        return;
    bool ok = false;
    const qreal pt = text.trimmed().toDouble(&ok);
    if (!ok || pt <= 0)
        return;
    QTextCursor cursor = m_editor->textCursor();
    if (cursor.hasSelection()) {
        QTextCharFormat fmt;
        fmt.setFontPointSize(pt);
        mergeFormatOnSelection(fmt);
    } else {
        m_baseFontSize = pt;
        applyBaseFont();
    }
    m_editor->setFocus();
}

void MainWindow::applyBaseFont()
{
    // Zoom is handled by the canvas view transform, so the document's own font
    // stays at its true point size (no rescaling on zoom).
    QFont f(m_baseFontFamily.isEmpty() ? m_editor->document()->defaultFont().family()
                                       : m_baseFontFamily);
    f.setPointSizeF(m_baseFontSize);
    m_editor->document()->setDefaultFont(f);
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
        QMessageBox::warning(this, tr("PagePad"), tr("Could not load image %1.").arg(fn));
        return;
    }

    const double maxW = m_editor->viewport()->width() - 2 * m_editor->document()->documentMargin();
    if (maxW > 0 && img.width() > maxW)
        img = img.scaledToWidth(static_cast<int>(maxW), Qt::SmoothTransformation);

    const QString name = QStringLiteral("img://%1.png").arg(QDateTime::currentMSecsSinceEpoch());
    m_editor->document()->addResource(QTextDocument::ImageResource, QUrl(name), img);

    QTextImageFormat fmt;
    fmt.setName(name);
    fmt.setWidth(img.width());
    fmt.setHeight(img.height());
    m_editor->textCursor().insertImage(fmt);
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

    const Qt::Alignment al = m_editor->alignment();
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

// ---------------------------------------------------------------- status

void MainWindow::updateWordCount()
{
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    const QString text = m_editor->toPlainText().trimmed();
    const int words = text.isEmpty() ? 0 : text.split(ws, Qt::SkipEmptyParts).size();
    m_wordLabel->setText(tr("Words: %1").arg(words));
}

void MainWindow::updatePageLabel()
{
    const int total = m_editor->pageCount();
    const qreal ph = m_editor->pageHeightPx();
    int current = 1;
    if (ph > 0) {
        const QRect cr = m_editor->cursorRect();
        current = qBound(1, static_cast<int>(cr.top() / ph) + 1, total);
    }
    m_pageLabel->setText(tr("Page %1 of %2").arg(current).arg(total));
}

void MainWindow::ensureCursorVisibleInScroll()
{
    if (!m_pageProxy || !m_view)
        return;
    // Map the caret (page/widget coords) into the scene, then keep it in view.
    const QRectF cr(m_editor->cursorRect());
    const QRectF sceneRect = m_pageProxy->mapToScene(cr).boundingRect();
    m_view->ensureVisible(sceneRect, 24, 48);
}

// ---------------------------------------------------------------- events

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (maybeSave())
        event->accept();
    else
        event->ignore();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    // Only re-fit on resize when in Fit-Width mode; Actual Size stays put.
    if (m_fitCombo && m_fitCombo->currentIndex() == 0)
        applyFitMode();
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
