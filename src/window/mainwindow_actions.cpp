// MainWindow — Signal/slot wiring for every menu, toolbar and tab-strip action.
//
// Part of the MainWindow implementation, split across several files by
// responsibility; see mainwindow.h for the class definition.

#include "window/mainwindow.h"
#include "window/keymap.h"
#include "widgets/settingsdialog.h"
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

// Install the platform keymap. Actions are found by object name, so the table
// in window/keymap.cpp is the one place shortcuts are defined.
void MainWindow::applyShortcuts()
{
    for (const auto &entry : Keymap::table(Keymap::current())) {
        QAction *action = findChild<QAction *>(entry.first);
        if (!action) {
            qWarning("Keymap names an action that does not exist: %s", qPrintable(entry.first));
            continue;
        }
        QList<QKeySequence> keys;
        for (const QString &k : entry.second)
            keys << QKeySequence(k, QKeySequence::PortableText);
        action->setShortcuts(keys);
    }
}

void MainWindow::connectActions()
{
    // File
    connect(ui->actionNew, &QAction::triggered, this, &MainWindow::newFile);
    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::openFile);
    connect(ui->actionSave, &QAction::triggered, this, &MainWindow::saveFile);
    connect(ui->actionSaveAs, &QAction::triggered, this, &MainWindow::saveFileAs);
    connect(ui->actionExportPdf, &QAction::triggered, this, &MainWindow::exportPdf);
    connect(ui->actionConvert, &QAction::triggered, this, &MainWindow::convertDocument);
    connect(ui->actionPrint, &QAction::triggered, this, &MainWindow::printDocument);
    connect(ui->actionShowInFolder, &QAction::triggered, this,
            [this] { showInFolder(m_doc); });
    connect(ui->actionSettings, &QAction::triggered, this, [this] {
        SettingsDialog(this).exec();
    });
    // Quit closes every window (each prompting for its unsaved tabs) and stops
    // at the first one that is cancelled — not just the window in front.
    connect(ui->actionQuit, &QAction::triggered, qApp, &QApplication::closeAllWindows);

    // Windows and tabs
    connect(ui->actionNewWindow, &QAction::triggered, this, [this] { createWindowFrom(this); });
    connect(ui->actionCloseTab, &QAction::triggered, this, &MainWindow::closeCurrentTab);
    connect(ui->actionCloseWindow, &QAction::triggered, this, &QWidget::close);
    connect(ui->actionReopenClosedTab, &QAction::triggered, this, &MainWindow::reopenClosedTab);
    connect(ui->actionMinimize, &QAction::triggered, this, &QWidget::showMinimized);
    connect(ui->actionFullScreen, &QAction::triggered, this, [this] {
        if (isFullScreen())
            showNormal();
        else
            showFullScreen();
    });
    connect(ui->actionMoveTabToNewWindow, &QAction::triggered, this, [this] {
        if (m_doc)
            detachToNewWindow(m_doc, frameGeometry().topLeft() + QPoint(110, 50));
    });
    // Minimise has no Windows/Linux shortcut and lives in the title bar there.
    if (Keymap::current() != Keymap::Platform::Mac)
        ui->actionMinimize->setVisible(false);

    // ⌘1…⌘9. Not in a menu, so they are added to the window directly; the
    // keymap assigns their keys by object name.
    for (int i = 1; i <= 9; ++i) {
        auto *jump = new QAction(this);
        jump->setObjectName(QStringLiteral("actionSelectTab%1").arg(i));
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
    connect(ui->actionPasteMatchStyle, &QAction::triggered, this,
            onEditor(&PageDocumentItem::pastePlainText));
    connect(ui->actionFindNext, &QAction::triggered, this, [this] { m_findBar->findNext(); });
    connect(ui->actionFindPrevious, &QAction::triggered, this, [this] { m_findBar->findPrevious(); });
    connect(ui->actionReplace, &QAction::triggered, this, [this] { m_findBar->activateReplace(); });

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
