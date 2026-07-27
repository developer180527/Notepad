// MainWindow — Text formatting: fonts, sizes, colours and insertions.
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
