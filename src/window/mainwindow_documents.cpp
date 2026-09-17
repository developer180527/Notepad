// MainWindow — Documents and tabs: adding, switching, closing, and moving documents
// between windows (tab detach and merge), plus the window registry.
//
// Part of the MainWindow implementation, split across several files by
// responsibility; see mainwindow.h for the class definition.

#include "window/mainwindow.h"
#include "ui_mainwindow.h"
#include "widgets/canvasview.h"
#include "document/documentview.h"
#include "widgets/documenttabbar.h"
#include "document/documenthighlighter.h"
#include "widgets/findbar.h"
#include "widgets/fontcombo.h"
#include "util/fontlibrary.h"
#include "widgets/iconfactory.h"
#include "document/pagedocumentitem.h"
#include "widgets/pagesetupdialog.h"
#include "widgets/rulerwidget.h"
#include <QActionGroup>
#include <QApplication>
#include <QScreen>
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
        if (doc == m_doc) {
            setWindowTitle(tr("%1[*] %2 Notepad").arg(doc->displayName(), QString(QChar(0x2014))));
            updateShowInFolderState();
        }
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

    rememberClosedTab(doc);
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
    // A torn-off tab should feel like it kept its window: same size as the one
    // it came from, not the saved default geometry.
    pending->resize(size());

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
    w->resize(size());             // inherit this window's size, not the default
    takeDocument(index);           // safe: guarded above, so we never self-close
    // Place before adopting: adoptDocument() shows the window, so moving
    // afterwards would make it appear at one position and jump to another.
    w->move(globalPos - QPoint(80, 20));
    w->adoptDocument(doc);
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

MainWindow *MainWindow::createWindowFrom(MainWindow *source)
{
    // Built without restoring saved geometry: restoreGeometry() also restores the
    // saved maximised/full-screen *state*, which Qt re-applies on show() and
    // would override the size taken from the source window.
    MainWindow *w = createWindowForAdoption();
    w->addDocument();
    w->setZoom(100);

    if (!source) {
        w->show();
        return w;
    }
    if (source->isMaximized() || source->isFullScreen()) {
        w->resize(source->normalGeometry().size());
        w->showMaximized();
        return w;
    }

    // Cascade down and to the right of the source, and wrap back to the top-left
    // of the screen rather than opening a window that hangs off its edge.
    constexpr int kCascade = 28;
    QRect frame(source->frameGeometry().topLeft() + QPoint(kCascade, kCascade),
                source->frameGeometry().size());
    if (QScreen *screen = source->screen()) {
        const QRect avail = screen->availableGeometry();
        if (!avail.contains(frame))
            frame.moveTopLeft(avail.topLeft() + QPoint(kCascade, kCascade));
    }
    w->resize(source->size());
    w->move(frame.topLeft());
    w->show();
    w->raise();
    w->activateWindow();
    return w;
}

// Closed tabs are remembered by file. An untitled document has no file to go
// back to, and one closed with "Don't Save" was discarded on purpose.
void MainWindow::rememberClosedTab(DocumentView *doc)
{
    if (!doc || doc->filePath().isEmpty())
        return;
    const QString path = QFileInfo(doc->filePath()).absoluteFilePath();
    s_closedTabs.removeAll(path);
    s_closedTabs.append(path);
    constexpr int kMaxRemembered = 25;
    while (s_closedTabs.size() > kMaxRemembered)
        s_closedTabs.removeFirst();
}

void MainWindow::reopenClosedTab()
{
    // Skip anything that has since been deleted or moved, or is already open.
    while (!s_closedTabs.isEmpty()) {
        const QString path = s_closedTabs.takeLast();
        if (!QFileInfo::exists(path))
            continue;
        bool alreadyOpen = false;
        for (MainWindow *w : std::as_const(s_windows))
            if (w->indexOfFile(path) >= 0)
                alreadyOpen = true;
        if (alreadyOpen)
            continue;
        openPath(path);
        return;
    }
    statusBar()->showMessage(tr("No recently closed tabs to reopen."), 2500);
}

// ⌘W closes the tab. When the window is down to a single untouched, untitled
// tab there is nothing left to close but the window itself — the same rule VS
// Code uses, so repeated ⌘W empties a window and then closes it.
void MainWindow::closeCurrentTab()
{
    if (m_stack->count() == 1 && m_doc && m_doc->filePath().isEmpty()
        && !m_doc->isModified() && m_doc->document()->isEmpty()) {
        close();
        return;
    }
    closeDocumentAt(m_tabs->currentIndex());
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
    updateShowInFolderState();
    updateWordCount();
    updatePageLabel();
    syncFormatControls();
}
