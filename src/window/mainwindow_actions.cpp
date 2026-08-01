// MainWindow — Signal/slot wiring for every menu, toolbar and tab-strip action.
//
// Part of the MainWindow implementation, split across several files by
// responsibility; see mainwindow.h for the class definition.

#include "window/mainwindow.h"
#include "widgets/settingsdialog.h"
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
    connect(ui->actionShowInFolder, &QAction::triggered, this,
            [this] { showInFolder(m_doc); });
    connect(ui->actionSettings, &QAction::triggered, this, [this] {
        SettingsDialog(this).exec();
    });
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
