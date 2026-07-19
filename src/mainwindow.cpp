#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "canvasview.h"
#include "documentview.h"
#include "documenttabbar.h"
#include "codehighlighter.h"
#include "findbar.h"
#include "fontcombo.h"
#include "fontlibrary.h"
#include "iconfactory.h"
#include "pagedocumentitem.h"
#include "pagesetupdialog.h"
#include "rulerwidget.h"

#include <QActionGroup>
#include <QApplication>
#include <QBuffer>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDataStream>
#include <QDateTime>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFileInfo>
#include <QFont>
#include <QGraphicsScene>
#include <QGraphicsOpacityEffect>
#include <QImage>
#include <QIntValidator>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPair>
#include <QPalette>
#include <QPdfWriter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPropertyAnimation>
#include <QSettings>
#include <QStandardPaths>
#include <QPixmap>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSet>
#include <QSlider>
#include <QSpinBox>
#include <QDrag>
#include <QMimeData>
#include <QStackedWidget>
#include <QTabBar>
#include <QStatusBar>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>

QList<MainWindow *> MainWindow::s_windows;
DocumentView *MainWindow::s_dragDoc = nullptr;
MainWindow *MainWindow::s_dragSource = nullptr;

namespace {
constexpr quint32 kNoteVersion = 3;   // v2 adds fonts; v3 adds a preview image
const char *kNoteMagic = "PPNOTE";
constexpr int kBarHeight = 36;        // shared menu bar / tool bar height
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : MainWindow(true, parent)
{
}

MainWindow::MainWindow(bool withInitialDocument, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);
    s_windows.prepend(this);

    FontLibrary::load();   // register bundled portable fonts

    // Render the menu bar inside the window (above the toolbar) on every
    // platform, instead of macOS's global menu bar — matches the design.
    ui->menubar->setNativeMenuBar(false);
    // Roomier, bolder, slightly lowered menu items. rgba hover works in both
    // light and dark themes; text colour is inherited so it adapts too.
    ui->menubar->setStyleSheet(QStringLiteral(
        "QMenuBar { padding: 0px 8px; font-size: 15px; font-weight: bold;"
        " border: 0px; }"
        "QMenuBar::item { padding: 4px 8px; margin: 0px 4px; background: transparent;"
        " border-radius: 6px; }"
        "QMenuBar::item:selected { background: rgba(128,128,128,0.28); }"
        "QMenuBar::item:pressed { background: rgba(128,128,128,0.40); }"));
    // Menu bar and tool bar read as one band: identical height, no frame or
    // gap between them.
    ui->menubar->setFixedHeight(kBarHeight);

    setupWorkspace();
    applyCanvasTheme();
    setupToolBar();
    setupStatusBar();
    connectActions();
    setupShortcutFeedback();
    refreshIcons();

    // Detaching a tab builds a window purely to receive an existing document;
    // creating an untitled one here (scene, canvas, ruler, page layout) only to
    // delete it again is the single biggest cost of the tear-off.
    if (withInitialDocument) {
        addDocument();
        setZoom(100);
    }

    // Restore the last window size/position.
    const QByteArray geom = QSettings().value(QStringLiteral("ui/geometry")).toByteArray();
    if (!geom.isEmpty())
        restoreGeometry(geom);

    if (m_doc)
        m_doc->editor()->setFocus();
    statusBar()->showMessage(tr("Ready"), 2000);
}

MainWindow::~MainWindow()
{
    s_windows.removeAll(this);
    delete ui;
}

void MainWindow::setupWorkspace()
{
    m_findBar = new FindBar(this);
    m_findBar->hide();

    m_stack = new QStackedWidget(this);
    m_tabs = new DocumentTabBar(this);

    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(m_findBar);
    centralLayout->addWidget(m_stack, 1);
    centralLayout->addWidget(m_tabs);      // tab strip sits just above the status bar
    setCentralWidget(central);

    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (index >= 0 && index < m_stack->count())
            m_stack->setCurrentIndex(index);
        bindDocument();
    });
    connect(m_tabs, &QTabBar::tabCloseRequested, this,
            [this](int index) { closeDocumentAt(index); });
    connect(m_tabs, &DocumentTabBar::newTabRequested, this, &MainWindow::newFile);
    connect(m_tabs, &DocumentTabBar::tabDragOut, this, &MainWindow::startTabDrag);
    connect(m_tabs, &DocumentTabBar::renameRequested, this,
            [this](int index, const QString &name) {
                DocumentView *doc = documentAt(index);
                if (!doc)
                    return;
                QString error;
                if (!doc->rename(name, &error)) {
                    QMessageBox::warning(this, tr("Rename"), error);
                    return;
                }
                updateTabLabel(doc);
                if (doc == m_doc) {
                    setWindowTitle(tr("%1[*] %2 Notepad")
                                       .arg(doc->displayName(), QString(QChar(0x2014))));
                    setWindowFilePath(doc->filePath());
                }
            });

    // Tab context menu.
    m_tabs->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabs, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const int index = m_tabs->tabAt(pos);
        if (index < 0)
            return;
        QMenu menu;
        QAction *rename = menu.addAction(tr("Rename..."));
        QAction *detach = menu.addAction(tr("Move to New Window"));
        detach->setEnabled(m_stack->count() > 1);
        menu.addSeparator();
        QAction *close = menu.addAction(tr("Close"));
        QAction *others = menu.addAction(tr("Close Others"));
        others->setEnabled(m_stack->count() > 1);
        QAction *right = menu.addAction(tr("Close to the Right"));
        right->setEnabled(index < m_stack->count() - 1);

        QAction *chosen = menu.exec(m_tabs->mapToGlobal(pos));
        if (chosen == rename) {
            m_tabs->beginRename(index);
        } else if (chosen == detach) {
            if (DocumentView *doc = documentAt(index))
                detachToNewWindow(doc, QCursor::pos());
        } else if (chosen == close) {
            closeDocumentAt(index);
        } else if (chosen == others) {
            DocumentView *keep = documentAt(index);
            // Walk backwards: closing shifts every later index down by one.
            for (int i = m_stack->count() - 1; i >= 0; --i)
                if (documentAt(i) != keep && !closeDocumentAt(i))
                    break;
        } else if (chosen == right) {
            for (int i = m_stack->count() - 1; i > index; --i)
                if (!closeDocumentAt(i))
                    break;
        }
    });
    connect(m_tabs, &DocumentTabBar::tabDropped, this, [this](int atIndex) {
        // A tab from another window (or this one) was dropped on our strip.
        DocumentView *doc = s_dragDoc;
        MainWindow *source = s_dragSource;
        if (!doc || !source)
            return;
        s_dragDoc = nullptr;                 // claimed: suppress the detach fallback
        if (source == this) {
            const int from = indexOfDocument(doc);
            if (from >= 0 && atIndex >= 0 && from != atIndex)
                m_tabs->moveTab(from, atIndex);
            return;
        }
        const int from = source->indexOfDocument(doc);
        if (from < 0)
            return;
        source->takeDocument(from);
        adoptDocument(doc, atIndex);
    });
    // Dragging a tab reorders the bar only; the stack has to follow so indices
    // keep lining up.
    connect(m_tabs, &QTabBar::tabMoved, this, [this](int from, int to) {
        QWidget *w = m_stack->widget(from);
        if (!w)
            return;
        m_stack->removeWidget(w);
        m_stack->insertWidget(to, w);
        m_stack->setCurrentIndex(m_tabs->currentIndex());
    });
}

DocumentView *MainWindow::documentAt(int index) const
{
    return qobject_cast<DocumentView *>(m_stack->widget(index));
}

int MainWindow::indexOfDocument(DocumentView *doc) const
{
    return doc ? m_stack->indexOf(doc) : -1;
}

int MainWindow::indexOfFile(const QString &path) const
{
    if (path.isEmpty())
        return -1;
    const QString target = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < m_stack->count(); ++i) {
        DocumentView *d = documentAt(i);
        if (d && !d->filePath().isEmpty()
            && QFileInfo(d->filePath()).absoluteFilePath() == target)
            return i;
    }
    return -1;
}

DocumentView *MainWindow::addDocument()
{
    auto *doc = new DocumentView(m_stack);
    const int index = m_stack->addWidget(doc);
    m_tabs->insertTab(index, doc->displayName());
    m_tabs->setDocumentLabel(index, doc->displayName(), false);

    registerDocument(doc);

    doc->setRulerVisible(ui->actionShowRuler->isChecked());
    applyCanvasTheme();

    m_tabs->setCurrentIndex(index);
    m_stack->setCurrentIndex(index);
    bindDocument();
    return doc;
}

// Window-level bookkeeping that must run for *every* document, current or not:
// a background tab still needs its label to show unsaved changes. Re-applied
// when a document is adopted from another window.
void MainWindow::registerDocument(DocumentView *doc)
{
    connect(doc, &DocumentView::modifiedChanged, this, [this, doc](bool) { updateTabLabel(doc); });
    connect(doc, &DocumentView::filePathChanged, this, [this, doc](const QString &) {
        updateTabLabel(doc);
        if (doc == m_doc)
            setWindowTitle(tr("%1[*] %2 Notepad").arg(doc->displayName(), QString(QChar(0x2014))));
    });
    connect(doc, &DocumentView::openFileRequested, this, &MainWindow::openPath);
}

void MainWindow::updateTabLabel(DocumentView *doc)
{
    const int index = indexOfDocument(doc);
    if (index >= 0)
        m_tabs->setDocumentLabel(index, doc->displayName(), doc->isModified());
    if (doc == m_doc)
        setWindowModified(doc->isModified());
}

// Point the window chrome at whichever document is now current. Connections
// that feed the toolbar/status bar are per-document, so they are torn down and
// rebuilt here rather than leaking across tab switches.
void MainWindow::bindDocument()
{
    for (const QMetaObject::Connection &c : std::as_const(m_docConnections))
        disconnect(c);
    m_docConnections.clear();

    m_doc = qobject_cast<DocumentView *>(m_stack->currentWidget());
    if (!m_doc)
        return;

    PageDocumentItem *ed = m_doc->editor();
    m_docConnections << connect(m_doc, &DocumentView::statusMessage, this,
                                [this](const QString &t, int ms) { statusBar()->showMessage(t, ms); });
    m_docConnections << connect(m_doc, &DocumentView::zoomChanged, this, [this](int percent) {
        if (m_updatingControls)
            return;
        m_updatingControls = true;
        m_zoomSlider->setValue(percent);
        m_zoomCombo->setCurrentText(QString::number(percent) + QStringLiteral("%"));
        m_zoomLabel->setText(QString::number(percent) + QStringLiteral("%"));
        m_updatingControls = false;
    });
    m_docConnections << connect(ed, &PageDocumentItem::undoAvailable,
                                ui->actionUndo, &QAction::setEnabled);
    m_docConnections << connect(ed, &PageDocumentItem::redoAvailable,
                                ui->actionRedo, &QAction::setEnabled);
    m_docConnections << connect(ed, &PageDocumentItem::selectionAvailable,
                                ui->actionCut, &QAction::setEnabled);
    m_docConnections << connect(ed, &PageDocumentItem::selectionAvailable,
                                ui->actionCopy, &QAction::setEnabled);
    m_docConnections << connect(ed, &PageDocumentItem::contentsChanged,
                                this, &MainWindow::updateWordCount);
    m_docConnections << connect(ed, &PageDocumentItem::pageCountChanged, this,
                                [this](int) { updatePageLabel(); });
    m_docConnections << connect(ed, &PageDocumentItem::cursorPositionChanged, this, [this] {
        syncFormatControls();
        updatePageLabel();
    });

    ui->actionUndo->setEnabled(m_doc->document()->isUndoAvailable());
    ui->actionRedo->setEnabled(m_doc->document()->isRedoAvailable());

    // The find bar belongs to the window; its highlights belong to a document.
    if (m_findBar->isVisible())
        m_findBar->dismiss();

    setWindowTitle(tr("%1[*] %2 Notepad").arg(m_doc->displayName(), QString(QChar(0x2014))));
    setWindowFilePath(m_doc->filePath());
    syncChromeToDocument();
    m_doc->editor()->setFocus();
}

bool MainWindow::maybeSaveDocument(DocumentView *doc)
{
    if (!doc || !doc->isModified())
        return true;
    // Show the user which document is being asked about — with several tabs open
    // an unqualified "the document" is ambiguous.
    const auto ret = QMessageBox::warning(
        this, tr("Notepad"),
        tr("\"%1\" has been modified.\nDo you want to save your changes?")
            .arg(doc->displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (ret == QMessageBox::Cancel)
        return false;
    if (ret == QMessageBox::Discard)
        return true;

    DocumentView *previous = m_doc;
    m_doc = doc;                   // save* act on m_doc
    const bool saved = saveFile();
    m_doc = previous;
    return saved;
}

bool MainWindow::closeDocumentAt(int index)
{
    DocumentView *doc = documentAt(index);
    if (!doc)
        return true;
    if (!maybeSaveDocument(doc))
        return false;

    m_stack->removeWidget(doc);
    m_tabs->removeTab(index);
    doc->deleteLater();

    if (m_stack->count() == 0)
        addDocument();             // never leave the window without a document
    else
        bindDocument();
    return true;
}

// A tab was pulled clear of the strip. Run a drag so it can be dropped on
// another window's strip (merge); if it lands nowhere, it becomes its own
// window (detach).
void MainWindow::startTabDrag(int index)
{
    DocumentView *doc = documentAt(index);
    if (!doc)
        return;
    // Dragging the only tab would just destroy and recreate this window for no
    // benefit — treat it as a no-op, like Chrome does.
    if (m_stack->count() <= 1)
        return;

    s_dragDoc = doc;
    s_dragSource = this;

    auto *mime = new QMimeData;
    mime->setData(DocumentTabBar::tabMimeType(), QByteArray::number(1));

    QDrag drag(this);
    drag.setMimeData(mime);
    const QRect tabRect = m_tabs->tabRect(index);
    if (tabRect.isValid())
        drag.setPixmap(m_tabs->grab(tabRect));
    drag.setHotSpot(QPoint(tabRect.width() / 2, tabRect.height() / 2));

    // Build the tear-off window *now*, while the user is still dragging, so a
    // detach only has to move and show it. Paying for the window after the drop
    // is what makes tear-off feel laggy. It stays hidden (and is discarded) if
    // the drop turns out to be a merge.
    MainWindow *pending = createWindowForAdoption();

    drag.exec(Qt::MoveAction);

    // Still ours after the drag? Then nothing accepted the drop — detach.
    if (s_dragDoc == doc) {
        // Position before adopting: adoptDocument() shows the window, and moving
        // afterwards would make it appear at the restored geometry and jump.
        pending->move(QCursor::pos() - QPoint(80, 20));
        takeDocument(indexOfDocument(doc));
        pending->adoptDocument(doc);
    } else {
        pending->deleteLater();      // merged elsewhere; the shell is unused
    }
    s_dragDoc = nullptr;
    s_dragSource = nullptr;
}

void MainWindow::detachToNewWindow(DocumentView *doc, const QPoint &globalPos)
{
    const int index = indexOfDocument(doc);
    if (index < 0 || m_stack->count() <= 1)
        return;

    MainWindow *w = createWindowForAdoption();
    takeDocument(index);           // safe: guarded above, so we never self-close
    w->adoptDocument(doc);
    // Place the new window near the cursor, offset so the title bar is grabbable.
    w->move(globalPos - QPoint(80, 20));
    w->show();
    w->raise();
    w->activateWindow();
}

// A window that starts empty, because a document is about to be moved into it.
// The normal constructor opens an untitled document, which would leave a stray
// blank tab beside the adopted one.
MainWindow *MainWindow::createWindowForAdoption()
{
    return new MainWindow(false, nullptr);
}

MainWindow *MainWindow::createWindow()
{
    auto *w = new MainWindow;      // registers itself; WA_DeleteOnClose owns it
    w->show();
    return w;
}

MainWindow *MainWindow::mostRecentWindow()
{
    return s_windows.isEmpty() ? nullptr : s_windows.first();
}

int MainWindow::documentCount() const
{
    return m_stack ? m_stack->count() : 0;
}

// The single entry point for externally requested opens (Finder/Explorer
// double-click, command line, a second instance forwarding its arguments).
void MainWindow::routeOpenPath(const QString &path)
{
    if (path.isEmpty())
        return;

    // Already open in some window? Surface that tab instead of a second copy.
    for (MainWindow *w : std::as_const(s_windows)) {
        const int index = w->indexOfFile(path);
        if (index >= 0) {
            w->m_tabs->setCurrentIndex(index);
            w->show();
            w->raise();
            w->activateWindow();
            return;
        }
    }

    // Skip windows that aren't on screen — a tear-off shell pre-built during a
    // drag is registered but hidden, and must not swallow the document.
    MainWindow *w = nullptr;
    for (MainWindow *candidate : std::as_const(s_windows)) {
        if (candidate->isVisible()) {
            w = candidate;
            break;
        }
    }
    if (!w)
        w = createWindow();
    w->openPath(path);
    w->show();
    w->raise();
    w->activateWindow();
}

// --- moving documents between windows (tab detach / merge) ---

DocumentView *MainWindow::takeDocument(int index)
{
    DocumentView *doc = documentAt(index);
    if (!doc)
        return nullptr;

    // Drop this window's bookkeeping connections; the new owner remakes them.
    disconnect(doc, nullptr, this, nullptr);

    m_stack->removeWidget(doc);
    m_tabs->removeTab(index);
    doc->setParent(nullptr);

    if (m_stack->count() == 0)
        close();                   // last document left: the window goes with it
    else
        bindDocument();
    return doc;
}

void MainWindow::adoptDocument(DocumentView *doc, int atIndex)
{
    if (!doc)
        return;
    const int index = (atIndex < 0 || atIndex > m_stack->count()) ? m_stack->count() : atIndex;

    doc->setParent(m_stack);
    m_stack->insertWidget(index, doc);
    m_tabs->insertTab(index, doc->displayName());
    m_tabs->setDocumentLabel(index, doc->displayName(), doc->isModified());
    registerDocument(doc);

    doc->setRulerVisible(ui->actionShowRuler->isChecked());
    applyCanvasTheme();
    m_tabs->setCurrentIndex(index);
    m_stack->setCurrentIndex(index);
    bindDocument();
    show();
    raise();
    activateWindow();
}


// Pull every piece of window chrome back into line with the current document.
// Phase 2 calls this on tab switch; today it just runs at startup and after a
// document is loaded.
void MainWindow::syncChromeToDocument()
{
    if (!m_doc)
        return;
    m_updatingControls = true;
    m_fontCombo->setCurrentFont(QFont(m_doc->baseFontFamily()));
    m_sizeCombo->setCurrentText(QString::number(qRound(m_doc->baseFontSize())));
    m_zoomSlider->setValue(m_doc->zoom());
    m_zoomCombo->setCurrentText(QString::number(m_doc->zoom()) + QStringLiteral("%"));
    m_zoomLabel->setText(QString::number(m_doc->zoom()) + QStringLiteral("%"));
    m_fitCombo->setCurrentIndex(m_doc->fitMode());
    ui->actionMarkdownSource->setChecked(m_doc->markdownSourceMode());
    m_updatingControls = false;

    setWindowModified(m_doc->isModified());
    updateMarkdownActionState();
    updateWordCount();
    updatePageLabel();
    syncFormatControls();
}

void MainWindow::setupToolBar()
{
    QToolBar *tb = addToolBar(tr("Main Toolbar"));
    tb->setObjectName(QStringLiteral("mainToolBar"));
    tb->setMovable(false);
    tb->setFloatable(false);
    tb->setIconSize(QSize(20, 20));
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
    // Match the menu bar exactly and drop the frame, so the two form a single
    // continuous band with no visible division.
    tb->setFixedHeight(kBarHeight);
    tb->setStyleSheet(QStringLiteral(
        "QToolBar { border: 0px; margin: 0px; padding: 0px 4px; spacing: 2px; }"
        "QToolBar::separator { width: 1px; margin: 6px 5px;"
        " background: rgba(128,128,128,0.35); }"));

    tb->addAction(ui->actionNew);
    tb->addAction(ui->actionOpen);
    tb->addAction(ui->actionSave);
    tb->addSeparator();
    tb->addAction(ui->actionUndo);
    tb->addAction(ui->actionRedo);
    tb->addSeparator();

    m_fontCombo = new FontCombo(tb);
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

    tb->addAction(ui->actionTextColor);
    tb->addAction(ui->actionHighlightColor);
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
    connect(ui->actionCloseTab, &QAction::triggered, this,
            [this] { closeDocumentAt(m_tabs->currentIndex()); });

    // Tab cycling. Not in a menu — these are muscle-memory shortcuts.
    ui->actionNextTab->setShortcuts({QKeySequence(QStringLiteral("Ctrl+Tab")),
                                     QKeySequence(QStringLiteral("Ctrl+PgDown"))});
    ui->actionPrevTab->setShortcuts({QKeySequence(QStringLiteral("Ctrl+Shift+Tab")),
                                     QKeySequence(QStringLiteral("Ctrl+PgUp"))});
    addAction(ui->actionNextTab);
    addAction(ui->actionPrevTab);

    // Ctrl/Cmd+1..8 select that tab; 9 always means "last", as in browsers.
    for (int i = 1; i <= 9; ++i) {
        auto *jump = new QAction(this);
        jump->setShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i)));
        connect(jump, &QAction::triggered, this, [this, i] {
            const int target = (i == 9) ? m_tabs->count() - 1 : i - 1;
            if (target >= 0 && target < m_tabs->count())
                m_tabs->setCurrentIndex(target);
        });
        addAction(jump);
    }
    connect(ui->actionNextTab, &QAction::triggered, this, [this] {
        if (m_tabs->count() > 1)
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count());
    });
    connect(ui->actionPrevTab, &QAction::triggered, this, [this] {
        if (m_tabs->count() > 1)
            m_tabs->setCurrentIndex((m_tabs->currentIndex() - 1 + m_tabs->count()) % m_tabs->count());
    });

    // Edit
    // These dispatch through m_doc at call time: the target document changes
    // with every tab switch, so they must not bind to one editor up front.
    auto onEditor = [this](void (PageDocumentItem::*fn)()) {
        return [this, fn] { if (m_doc) (m_doc->editor()->*fn)(); };
    };
    connect(ui->actionUndo, &QAction::triggered, this, onEditor(&PageDocumentItem::undo));
    connect(ui->actionRedo, &QAction::triggered, this, onEditor(&PageDocumentItem::redo));
    connect(ui->actionCut, &QAction::triggered, this, onEditor(&PageDocumentItem::cut));
    connect(ui->actionCopy, &QAction::triggered, this, onEditor(&PageDocumentItem::copy));
    connect(ui->actionPaste, &QAction::triggered, this, onEditor(&PageDocumentItem::paste));
    connect(ui->actionSelectAll, &QAction::triggered, this, onEditor(&PageDocumentItem::selectAll));

    ui->actionUndo->setEnabled(false);
    ui->actionRedo->setEnabled(false);
    ui->actionCut->setEnabled(false);
    ui->actionCopy->setEnabled(false);

    // Find / replace
    // Ctrl/Cmd+F toggles: summon the bar, or dismiss it if it's already up.
    connect(ui->actionFind, &QAction::triggered, this, [this] {
        if (m_findBar->isVisible())
            m_findBar->dismiss();
        else
            m_findBar->activate();
    });
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
                if (!m_doc->editor()->find(text, findFlags(fwd, cs, whole)) && !text.isEmpty())
                    statusBar()->showMessage(tr("Not found: %1").arg(text), 2000);
            });
    connect(m_findBar, &FindBar::replaceRequested, this,
            [this, findFlags](const QString &text, const QString &with, bool cs, bool whole) {
                m_doc->editor()->replaceSelection(text, with, findFlags(true, cs, whole));
            });
    connect(m_findBar, &FindBar::replaceAllRequested, this,
            [this, findFlags](const QString &text, const QString &with, bool cs, bool whole) {
                const int n = m_doc->editor()->replaceAll(text, with, findFlags(true, cs, whole));
                statusBar()->showMessage(tr("Replaced %n occurrence(s)", nullptr, n), 2500);
            });
    connect(m_findBar, &FindBar::highlightRequested, this,
            [this, findFlags](const QString &text, bool cs, bool whole) {
                m_doc->editor()->setSearchHighlight(text, findFlags(true, cs, whole));
            });
    connect(m_findBar, &FindBar::closed, this, [this] {
        m_doc->editor()->setSearchHighlight(QString(), {});   // clear yellow highlights
        m_doc->editor()->setFocus();
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

    connect(ui->actionTextColor, &QAction::triggered, this, &MainWindow::chooseTextColor);
    connect(ui->actionHighlightColor, &QAction::triggered, this, &MainWindow::chooseHighlightColor);

    connect(ui->actionAlignLeft, &QAction::triggered, this,
            [this] { m_doc->editor()->setAlignmentValue(Qt::AlignLeft); });
    connect(ui->actionAlignCenter, &QAction::triggered, this,
            [this] { m_doc->editor()->setAlignmentValue(Qt::AlignHCenter); });
    connect(ui->actionAlignRight, &QAction::triggered, this,
            [this] { m_doc->editor()->setAlignmentValue(Qt::AlignRight); });
    connect(ui->actionAlignJustify, &QAction::triggered, this,
            [this] { m_doc->editor()->setAlignmentValue(Qt::AlignJustify); });

    connect(m_fontCombo, &FontCombo::currentFontChanged, this,
            &MainWindow::onFontFamilyChanged);
    connect(m_sizeCombo, &QComboBox::textActivated, this, &MainWindow::onFontSizeChanged);
    // textActivated only fires for items already in the list, so a typed-in size
    // (e.g. 37) needs the line edit's Return to apply as well.
    if (QLineEdit *sizeEdit = m_sizeCombo->lineEdit())
        connect(sizeEdit, &QLineEdit::returnPressed, this,
                [this] { onFontSizeChanged(m_sizeCombo->currentText()); });

    // View / zoom
    connect(ui->actionZoomIn, &QAction::triggered, this, [this] { setZoom(m_doc->zoom() + 10); });
    connect(ui->actionZoomOut, &QAction::triggered, this, [this] { setZoom(m_doc->zoom() - 10); });
    connect(ui->actionResetZoom, &QAction::triggered, this, [this] { setZoom(100); });
    connect(ui->actionShowRuler, &QAction::toggled, this, [this](bool on) {
        for (int i = 0; i < m_stack->count(); ++i)
            documentAt(i)->setRulerVisible(on);   // a view preference, not a document one
    });
    connect(ui->actionMarkdownSource, &QAction::toggled, this, [this](bool on) {
        if (m_updatingControls)
            return;
        m_doc->setMarkdownSourceMode(on);
        syncChromeToDocument();
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
    connect(m_fitCombo, &QComboBox::currentIndexChanged, this,
            [this](int idx) { if (!m_updatingControls) m_doc->setFitMode(idx); });

    // Help
    connect(ui->actionAbout, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("About Notepad"),
                           tr("<b>Notepad</b><br>A small, fast, paginated text editor.<br><br>"
                              "Built with Qt %1.").arg(QStringLiteral(QT_VERSION_STR)));
    });
    connect(ui->actionAboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);

    // Editor state -> UI
}

// When an action runs from a keyboard shortcut there's no visual feedback at
// all — the menu never opens. Briefly glow the menu that owns the action so the
// user can see what fired (and learn where it lives).
void MainWindow::setupShortcutFeedback()
{
    const QList<QMenu *> menus = {ui->menuFile, ui->menuEdit, ui->menuInsert,
                                  ui->menuFormat, ui->menuView, ui->menuHelp};
    for (QMenu *menu : menus) {
        QAction *title = menu->menuAction();
        for (QAction *a : menu->actions()) {
            if (a->isSeparator() || a->shortcut().isEmpty())
                continue;
            connect(a, &QAction::triggered, this, [this, title] {
                // A popup is up only when the user picked the item from the menu
                // by hand; in that case they already have feedback.
                if (QApplication::activePopupWidget())
                    return;
                flashMenu(title);
            });
        }
    }
}

void MainWindow::flashMenu(QAction *menuAction)
{
    const QRect r = ui->menubar->actionGeometry(menuAction);
    if (r.isEmpty())
        return;

    auto *glow = new QWidget(ui->menubar);
    glow->setGeometry(r);
    glow->setAttribute(Qt::WA_TransparentForMouseEvents);
    const QColor hl = palette().color(QPalette::Highlight);
    glow->setStyleSheet(QStringLiteral("background: rgba(%1,%2,%3,120); border-radius: 5px;")
                            .arg(hl.red()).arg(hl.green()).arg(hl.blue()));

    auto *fade = new QGraphicsOpacityEffect(glow);
    fade->setOpacity(1.0);
    glow->setGraphicsEffect(fade);
    glow->show();
    glow->raise();

    auto *anim = new QPropertyAnimation(fade, "opacity", glow);
    anim->setDuration(550);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QPropertyAnimation::finished, glow, &QObject::deleteLater);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
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
    ui->actionTextColor->setIcon(IconFactory::textColor(c));
    ui->actionHighlightColor->setIcon(IconFactory::highlight(c));
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
    if (!m_stack)
        return;                       // called once before any document exists
    for (int i = 0; i < m_stack->count(); ++i)
        if (DocumentView *d = documentAt(i))
            d->canvas()->setBackgroundBrush(canvasColor);
}

// ---------------------------------------------------------------- file ops

// New/Open no longer disturb what's already on screen — they add a tab.
void MainWindow::newFile()
{
    addDocument();
}

void MainWindow::openFile()
{
    if (!maybeSave())
        return;
    QSettings settings;
    const QString lastDir = usableDir(settings.value(QStringLiteral("io/lastDir")).toString());
    const QString fn = QFileDialog::getOpenFileName(
        this, tr("Open"), lastDir,
        tr("All Supported (*.note *.txt *.md *.markdown *.html *.htm *.json *.yaml *.yml);;"
           "Notepad Note (*.note);;Text (*.txt);;Markdown (*.md *.markdown);;"
           "HTML (*.html *.htm);;JSON (*.json);;YAML (*.yaml *.yml);;All Files (*)"));
    if (fn.isEmpty())
        return;
    settings.setValue(QStringLiteral("io/lastDir"), QFileInfo(fn).absolutePath());
    openPath(fn);
}

void MainWindow::openPath(const QString &path)
{
    if (path.isEmpty())
        return;

    // Already open here? Just bring that tab forward rather than loading a
    // second copy that could diverge from the first.
    const int existing = indexOfFile(path);
    if (existing >= 0) {
        m_tabs->setCurrentIndex(existing);
        return;
    }

    // Reuse the current tab only if it's a pristine, untitled, empty document —
    // otherwise the user's work would be replaced.
    DocumentView *target = m_doc;
    const bool reusable = target && target->filePath().isEmpty() && !target->isModified()
                          && target->document()->isEmpty();
    if (!reusable)
        target = addDocument();

    QString error;
    if (!target->load(path, &error)) {
        QMessageBox::warning(this, tr("Notepad"), error);
        if (!reusable)
            closeDocumentAt(indexOfDocument(target));
        return;
    }
    target->setFilePath(path);
    target->document()->setModified(false);
    updateTabLabel(target);
    if (target == m_doc)
        syncChromeToDocument();
    setWindowTitle(tr("%1[*] %2 Notepad").arg(target->displayName(), QString(QChar(0x2014))));
    setWindowFilePath(path);
    updateMarkdownActionState();
}

// A remembered directory is only useful if it still exists — folders get
// renamed and moved between sessions. Falls back to Documents.
QString MainWindow::usableDir(const QString &dir)
{
    if (!dir.isEmpty() && QFileInfo(dir).isDir())
        return dir;
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

bool MainWindow::saveFile()
{
    if (m_doc->filePath().isEmpty())
        return saveFileAs();

    // The document's folder may have been renamed, moved or deleted since it was
    // opened. Writing would just fail with "cannot open", so explain what
    // happened and let the user re-place the file instead.
    if (!QFileInfo(m_doc->filePath()).absoluteDir().exists()) {
        QMessageBox::information(
            this, tr("Notepad"),
            tr("The folder for \"%1\" no longer exists — it may have been renamed, "
               "moved or deleted.\n\nChoose where to save the document.")
                .arg(QFileInfo(m_doc->filePath()).fileName()));
        return saveFileAs();
    }
    return writeToFile(m_doc->filePath());
}

bool MainWindow::saveFileAs()
{
    QSettings settings;
    // Restore the last-used directory and format/filter from the previous save.
    QString selectedFilter = settings.value(QStringLiteral("io/lastSaveFilter")).toString();
    QString suggested = m_doc->filePath();
    // If the current document's folder vanished (renamed/moved), keep the file
    // name but re-anchor it to a directory that still exists.
    if (!suggested.isEmpty() && !QFileInfo(suggested).absoluteDir().exists())
        suggested = usableDir(QString()) + QLatin1Char('/') + QFileInfo(suggested).fileName();
    if (suggested.isEmpty()) {
        const QString lastDir = usableDir(settings.value(QStringLiteral("io/lastDir")).toString());
        // A name given by renaming the tab becomes the suggestion here, so
        // naming a scratch document up front actually pays off at save time.
        QString base = m_doc->displayName();
        if (base == tr("Untitled"))
            base = QStringLiteral("Untitled.note");
        else if (QFileInfo(base).suffix().isEmpty())
            base += QStringLiteral(".note");
        suggested = lastDir + QLatin1Char('/') + base;
    }
    QString fn = QFileDialog::getSaveFileName(
        this, tr("Save As"), suggested,
        tr("Notepad Note (*.note);;Text (*.txt);;Markdown (*.md);;HTML (*.html);;"
           "JSON (*.json);;YAML (*.yaml *.yml)"),
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
        else if (selectedFilter.contains(QStringLiteral(".json")))
            fn += QStringLiteral(".json");
        else if (selectedFilter.contains(QStringLiteral(".yaml")))
            fn += QStringLiteral(".yaml");
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


bool MainWindow::confirmLossySave(const QString &suffix)
{
    const bool isTxt = (suffix == QLatin1String("txt") || suffix == QLatin1String("json")
                        || suffix == QLatin1String("yaml") || suffix == QLatin1String("yml"));
    const bool isMd  = (suffix == QLatin1String("md") || suffix == QLatin1String("markdown"));
    if (!isTxt && !isMd)
        return true;   // .note / .html are lossless
    if (!m_doc->hasRichFormatting())
        return true;   // nothing would be lost

    const QString detail = isTxt
        ? tr("Plain text can't store any formatting — colors, fonts, sizes, "
             "tables and images will be discarded.")
        : tr("Markdown keeps bold, italic, headings, lists and tables, but "
             "discards colors, fonts, sizes and images.");

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Save as %1?").arg(suffix.toUpper()));
    box.setText(tr("This format loses formatting that a .note file keeps."));
    box.setInformativeText(detail + QStringLiteral("\n\n")
                           + tr("Save as .note to preserve everything."));
    box.setStandardButtons(QMessageBox::Save | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    return box.exec() == QMessageBox::Save;
}

bool MainWindow::writeToFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (!confirmLossySave(suffix))
        return false;

    QString error;
    if (!m_doc->save(path, &error)) {
        QMessageBox::warning(this, tr("Notepad"), error);
        return false;
    }
    setWindowModified(false);
    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(path).fileName()), 2000);
    return true;
}



void MainWindow::updateMarkdownActionState()
{
    const bool isMd = m_doc->isMarkdownFile();
    ui->actionMarkdownSource->setEnabled(isMd);
    if (!isMd)
        ui->actionMarkdownSource->setChecked(false);
}

bool MainWindow::loadDocument(const QString &path)
{
    QString error;
    if (!m_doc->load(path, &error)) {
        QMessageBox::warning(this, tr("Notepad"), error);
        return false;
    }
    syncChromeToDocument();
    return true;
}

void MainWindow::setCurrentFile(const QString &path)
{
    m_doc->setFilePath(path);
    m_doc->document()->setModified(false);
    setWindowModified(false);
    setWindowTitle(tr("%1[*] %2 Notepad").arg(m_doc->displayName(), QString(QChar(0x2014))));
    setWindowFilePath(path);
    updateMarkdownActionState();
}

bool MainWindow::maybeSave()
{
    return maybeSaveDocument(m_doc);
}

void MainWindow::exportPdf()
{
    QString suggested = m_doc->filePath().isEmpty()
                            ? QStringLiteral("Untitled.pdf")
                            : QFileInfo(m_doc->filePath()).completeBaseName() + QStringLiteral(".pdf");
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
    QTextDocument *doc = m_doc->editor()->document()->clone(this);
    QFont f(m_doc->baseFontFamily());
    f.setPointSizeF(m_doc->baseFontSize());
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
    QTextDocument *doc = m_doc->editor()->document()->clone(this);
    QFont f(m_doc->baseFontFamily());
    f.setPointSizeF(m_doc->baseFontSize());
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
    m_doc->editor()->mergeFormatOnSelection(format);
    m_doc->editor()->setFocus();
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
    qreal cur = m_doc->editor()->currentCharFormat().fontPointSize();
    if (cur <= 0)
        cur = m_doc->baseFontSize();
    QTextCharFormat fmt;
    fmt.setFontPointSize(qBound(1.0, qRound(cur) + qreal(delta), 999.0));
    mergeFormatOnSelection(fmt);
}

void MainWindow::chooseTextColor()
{
    // Seed the dialog with the selection's current colour, then the last one used.
    QColor initial = m_doc->editor()->currentCharFormat().foreground().color();
    if (!m_doc->editor()->currentCharFormat().hasProperty(QTextFormat::ForegroundBrush))
        initial = m_lastTextColor.isValid() ? m_lastTextColor : QColor(Qt::black);
    const QColor c = QColorDialog::getColor(initial, this, tr("Text Color"));
    if (!c.isValid())
        return;
    m_lastTextColor = c;
    QTextCharFormat fmt;
    fmt.setForeground(c);
    mergeFormatOnSelection(fmt);
}

void MainWindow::chooseHighlightColor()
{
    QColor initial = m_lastHighlightColor.isValid() ? m_lastHighlightColor
                                                     : QColor(255, 255, 0);
    const QColor c = QColorDialog::getColor(
        initial, this, tr("Highlight Color"), QColorDialog::ShowAlphaChannel);
    if (!c.isValid())
        return;
    m_lastHighlightColor = c;
    QTextCharFormat fmt;
    // Fully transparent acts as "no highlight" (clears any existing background).
    if (c.alpha() == 0)
        fmt.setBackground(Qt::NoBrush);
    else
        fmt.setBackground(c);
    mergeFormatOnSelection(fmt);
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
    m_doc->editor()->insertImage(img);
    m_doc->editor()->setFocus();
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
        m_doc->editor()->insertTable(rows->value(), cols->value());
        m_doc->editor()->setFocus();
    }
}

void MainWindow::syncFormatControls()
{
    if (m_updatingControls)
        return;
    m_updatingControls = true;

    const QTextCharFormat fmt = m_doc->editor()->currentCharFormat();
    ui->actionBold->setChecked(fmt.fontWeight() >= QFont::Bold);
    ui->actionItalic->setChecked(fmt.fontItalic());
    ui->actionUnderline->setChecked(fmt.fontUnderline());

    QString family = fmt.fontFamilies().toStringList().value(0);
    if (family.isEmpty())
        family = m_doc->editor()->document()->defaultFont().family();
    m_fontCombo->setCurrentFont(QFont(family));

    const qreal pt = fmt.fontPointSize();
    m_sizeCombo->setCurrentText(
        QString::number(pt > 0 ? qRound(pt) : qRound(m_doc->baseFontSize())));

    const Qt::Alignment al = m_doc->editor()->alignmentValue();
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
    m_doc->setZoom(percent);
    m_updatingControls = true;
    m_zoomSlider->setValue(m_doc->zoom());
    m_zoomCombo->setCurrentText(QString::number(m_doc->zoom()) + QStringLiteral("%"));
    m_zoomLabel->setText(QString::number(m_doc->zoom()) + QStringLiteral("%"));
    m_updatingControls = false;
}


void MainWindow::openPageSetup()
{
    PageSetupDialog dialog(m_doc->paper(), m_doc->orientation(), m_doc->marginsMm(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_doc->setPageSetup(dialog.paperSize(), dialog.orientation(), dialog.marginsMm());
    updatePageLabel();
}

// ---------------------------------------------------------------- status

void MainWindow::updateWordCount()
{
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    const QString text = m_doc->editor()->document()->toPlainText().trimmed();
    const int words = text.isEmpty() ? 0 : text.split(ws, Qt::SkipEmptyParts).size();
    m_wordLabel->setText(tr("Words: %1").arg(words));
}

void MainWindow::updatePageLabel()
{
    m_pageLabel->setText(tr("Page %1 of %2").arg(m_doc->editor()->currentPage()).arg(m_doc->editor()->pageCount()));
}

// ---------------------------------------------------------------- events

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Every tab gets its own prompt; cancelling any one aborts the whole close.
    for (int i = 0; i < m_stack->count(); ++i) {
        DocumentView *doc = documentAt(i);
        if (doc && doc->isModified()) {
            m_tabs->setCurrentIndex(i);      // show what's being asked about
            if (!maybeSaveDocument(doc)) {
                event->ignore();
                return;
            }
        }
    }
    QSettings().setValue(QStringLiteral("ui/geometry"), saveGeometry());
    event->accept();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    // Only re-fit on resize when in Fit-Width mode; Actual Size stays put.
    if (m_doc && m_doc->fitMode() == 0)
        m_doc->applyFitMode();
    if (m_doc)
        m_doc->ruler()->update();
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && isActiveWindow()) {
        s_windows.removeAll(this);      // keep most-recently-active at the front
        s_windows.prepend(this);
    }
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
