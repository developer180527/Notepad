// MainWindow — File operations: new, open, save, export, print and reveal-in-folder.
//
// Part of the MainWindow implementation, split across several files by
// responsibility; see mainwindow.h for the class definition.

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

// Reveal the document in the platform's file manager, with the file itself
// selected. Each platform needs its own incantation; where the "select the
// item" form isn't available we fall back to opening the containing folder.
void MainWindow::showInFolder(DocumentView *doc)
{
    if (!doc)
        return;
    const QString path = doc->filePath();
    if (path.isEmpty()) {
        statusBar()->showMessage(tr("Save the document first to show it in a folder."), 3000);
        return;
    }
    const QFileInfo info(path);
    if (!info.exists()) {
        QMessageBox::warning(this, tr("Notepad"),
                             tr("\"%1\" is no longer on disk — it may have been moved, "
                                "renamed or deleted.").arg(info.fileName()));
        return;
    }

    const QString native = QDir::toNativeSeparators(info.absoluteFilePath());
#if defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), native});
#elif defined(Q_OS_WIN)
    // explorer wants the switch and path as one argument, comma-separated.
    QProcess::startDetached(QStringLiteral("explorer"),
                            {QStringLiteral("/select,") + native});
#else
    // The freedesktop file-manager interface selects the file; not every desktop
    // provides it, so fall back to just opening the directory.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.FileManager1"),
        QStringLiteral("/org/freedesktop/FileManager1"),
        QStringLiteral("org.freedesktop.FileManager1"),
        QStringLiteral("ShowItems"));
    msg << QStringList{QUrl::fromLocalFile(info.absoluteFilePath()).toString()}
        << QString();
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 1000);
    if (reply.type() == QDBusMessage::ErrorMessage)
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
#endif
}

// Only meaningful once the document exists on disk.
void MainWindow::updateShowInFolderState()
{
    ui->actionShowInFolder->setEnabled(m_doc && !m_doc->filePath().isEmpty());
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
