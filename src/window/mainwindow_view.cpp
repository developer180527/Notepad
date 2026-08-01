// MainWindow — View and status: zoom, page setup and the status-bar readouts.
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
