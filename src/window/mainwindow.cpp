#include "window/mainwindow.h"
#include "ui_mainwindow.h"

#include "widgets/canvasview.h"
#include "document/documentview.h"
#include "widgets/documenttabbar.h"
#include "document/codehighlighter.h"
#include "widgets/findbar.h"
#include "widgets/fontcombo.h"
#include "util/fontlibrary.h"
#include "widgets/iconfactory.h"
#include "document/pagedocumentitem.h"
#include "widgets/pagesetupdialog.h"
#include "widgets/rulerwidget.h"

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
#include <QDesktopServices>
#include <QDesktopServices>
#include <QDrag>
#include <QProcess>
#include <QProcess>
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

#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#endif

#include <cmath>

QList<MainWindow *> MainWindow::s_windows;
DocumentView *MainWindow::s_dragDoc = nullptr;
MainWindow *MainWindow::s_dragSource = nullptr;

namespace {
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

    // Restore the last window size/position — but only for a real new window.
    // A tear-off shell takes its size from the window the tab came from, and
    // restoreGeometry() also restores the saved *state* (maximized/fullscreen),
    // which Qt re-applies on show() and would override that size.
    if (withInitialDocument) {
        const QByteArray geom = QSettings().value(QStringLiteral("ui/geometry")).toByteArray();
        if (!geom.isEmpty())
            restoreGeometry(geom);
    }

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
                    updateShowInFolderState();
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
        QAction *reveal = menu.addAction(tr("Show in Folder"));
        reveal->setEnabled(documentAt(index) && !documentAt(index)->filePath().isEmpty());
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
        } else if (chosen == reveal) {
            showInFolder(documentAt(index));
        } else if (chosen == reveal) {
            showInFolder(documentAt(index));
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


// --- moving documents between windows (tab detach / merge) ---


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


// ---------------------------------------------------------------- format


// ---------------------------------------------------------------- view


// ---------------------------------------------------------------- status


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
