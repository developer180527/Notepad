#include "documentview.h"

#include "canvasview.h"
#include "codehighlighter.h"
#include "fontlibrary.h"
#include "pagedocumentitem.h"
#include "rulerwidget.h"

#include <QBuffer>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QGraphicsScene>
#include <QImage>
#include <QList>
#include <QPair>
#include <QScrollBar>
#include <QSet>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextFragment>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr quint32 kNoteVersion = 3;   // v2 adds fonts; v3 adds a preview image
const char *kNoteMagic = "PPNOTE";
} // namespace

DocumentView::DocumentView(QWidget *parent)
    : QWidget(parent)
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

    m_ruler = new RulerWidget(m_view, m_editor, this);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_ruler);
    layout->addWidget(m_view, 1);

    m_baseFontFamily = FontLibrary::defaultFamily().isEmpty()
                           ? m_editor->document()->defaultFont().family()
                           : FontLibrary::defaultFamily();

    // The ruler must repaint whenever the page moves under it (zoom or scroll).
    connect(m_view, &CanvasView::zoomChanged, this, [this](qreal f) {
        m_ruler->update();
        const int percent = qRound(f * 100.0);
        if (percent != m_zoom) {
            m_zoom = percent;
            emit zoomChanged(percent);
        }
    });
    connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int) { m_ruler->update(); });

    connect(m_editor->document(), &QTextDocument::modificationChanged, this,
            &DocumentView::modifiedChanged);
    connect(m_editor, &PageDocumentItem::pageCountChanged, this,
            [this](int) { updateSceneRect(); });
    connect(m_editor, &PageDocumentItem::ensureVisibleRequested, this, [this](const QRectF &r) {
        m_view->ensureVisible(r, 24, 48);
    });
    connect(m_editor, &PageDocumentItem::openFileRequested, this,
            &DocumentView::openFileRequested);

    applyBaseFont();
    applyPageSetup();
    updateSceneRect();

    // Setting the default font and page metrics counts as touching the document,
    // so a brand-new view would otherwise report itself as modified — which
    // would prompt to save an untouched tab and stop "open" from reusing it.
    m_editor->document()->setModified(false);
}

QTextDocument *DocumentView::document() const
{
    return m_editor->document();
}

bool DocumentView::isModified() const
{
    return m_editor->document()->isModified();
}

QString DocumentView::displayName() const
{
    if (!m_filePath.isEmpty())
        return QFileInfo(m_filePath).fileName();
    return m_chosenName.isEmpty() ? tr("Untitled") : m_chosenName;
}

bool DocumentView::rename(const QString &newName, QString *errorOut)
{
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty() || trimmed == displayName())
        return true;                       // nothing to do

    // Reject anything that would move the file somewhere else; renaming a tab
    // should never silently relocate the document.
    if (trimmed.contains(QLatin1Char('/')) || trimmed.contains(QLatin1Char('\\'))) {
        if (errorOut)
            *errorOut = tr("A name cannot contain path separators.");
        return false;
    }

    if (m_filePath.isEmpty()) {
        m_chosenName = trimmed;            // unsaved: remembered for Save As
        emit filePathChanged(m_filePath);
        return true;
    }

    const QFileInfo info(m_filePath);
    // Typing a bare name keeps the extension, so renaming "notes.md" to
    // "journal" doesn't quietly turn it into an extensionless file.
    QString finalName = trimmed;
    if (QFileInfo(trimmed).suffix().isEmpty() && !info.suffix().isEmpty())
        finalName += QLatin1Char('.') + info.suffix();

    const QString newPath = info.absoluteDir().absoluteFilePath(finalName);
    if (newPath == info.absoluteFilePath())
        return true;
    if (QFileInfo::exists(newPath)) {
        if (errorOut)
            *errorOut = tr("\"%1\" already exists in this folder.").arg(finalName);
        return false;
    }
    QFile file(m_filePath);
    if (!file.rename(newPath)) {
        if (errorOut)
            *errorOut = tr("Could not rename to \"%1\":\n%2").arg(finalName, file.errorString());
        return false;
    }

    m_filePath = newPath;
    emit filePathChanged(newPath);
    return true;
}

bool DocumentView::isMarkdownFile() const
{
    const QString suffix = QFileInfo(m_filePath).suffix().toLower();
    return suffix == QLatin1String("md") || suffix == QLatin1String("markdown");
}

void DocumentView::setFilePath(const QString &path)
{
    if (m_filePath == path)
        return;
    m_filePath = path;
    if (!isMarkdownFile())
        m_mdSourceMode = false;     // leaving Markdown: drop back to rendered
    emit filePathChanged(path);
}

void DocumentView::updateSceneRect()
{
    if (!m_scene || !m_editor)
        return;
    // Pad generously around the pages so they can be panned freely on the canvas.
    const qreal pad = 260.0;
    m_scene->setSceneRect(m_editor->boundingRect().adjusted(-pad, -pad, pad, pad));
}

void DocumentView::setRulerVisible(bool visible)
{
    m_ruler->setVisible(visible);
}

// ---------------------------------------------------------------- fonts

void DocumentView::setBaseFontFamily(const QString &family)
{
    m_baseFontFamily = family;
    applyBaseFont();
}

void DocumentView::setBaseFontSize(qreal pt)
{
    m_baseFontSize = pt;
    applyBaseFont();
}

void DocumentView::applyBaseFont()
{
    // Font family/size set the document's base font (whole document). Zoom is a
    // separate canvas transform, so the point size stays true.
    QFont f(m_baseFontFamily.isEmpty() ? m_editor->document()->defaultFont().family()
                                       : m_baseFontFamily);
    f.setPointSizeF(m_baseFontSize);
    m_editor->setBaseFont(f);
}

// ---------------------------------------------------------------- zoom / fit

void DocumentView::setZoom(int percent)
{
    percent = qBound(25, percent, 400);
    if (m_zoom == percent && m_view->zoomFactor() == percent / 100.0)
        return;
    m_zoom = percent;
    // Zoom magnifies the whole canvas via a view transform; the page keeps its
    // fixed size and the document is not relaid out.
    m_view->setZoomFactor(percent / 100.0);
    emit zoomChanged(percent);
}

void DocumentView::setFitMode(int mode)
{
    m_fitMode = mode;
    applyFitMode();
}

void DocumentView::applyFitMode()
{
    if (!m_view || !m_view->viewport())
        return;
    if (m_fitMode == 1) {
        setZoom(100);   // Actual size
        return;
    }
    // Fit width: choose a zoom so the fixed-width page fills the viewport.
    const int viewportW = m_view->viewport()->width();
    const int pageW = m_editor->pageWidthPx();
    if (pageW > 0)
        setZoom(qRound((viewportW - 40) * 100.0 / pageW));
}

// ---------------------------------------------------------------- page setup

void DocumentView::setPageSetup(QPageSize::PageSizeId paper,
                                QPageLayout::Orientation orientation,
                                const QMarginsF &marginsMm)
{
    m_paperId = paper;
    m_orientation = orientation;
    m_marginsMm = marginsMm;
    applyPageSetup();
}

void DocumentView::applyPageSetup()
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
    if (m_fitMode == 0)
        applyFitMode();
    if (m_ruler)
        m_ruler->update();
}

// ---------------------------------------------------------------- view modes

// Data/code files (JSON, YAML) get a monospace page and syntax colouring;
// everything else reverts to the normal prose font with highlighting off.
void DocumentView::applySyntaxMode(const QString &suffix)
{
    if (!m_highlighter)
        m_highlighter = new CodeHighlighter(m_editor->document());

    const CodeHighlighter::Language lang = CodeHighlighter::languageForSuffix(suffix);
    if (lang != CodeHighlighter::Language::None) {
        m_baseFontFamily = QStringLiteral("JetBrains Mono");
        applyBaseFont();
    }
    m_highlighter->setLanguage(lang);
}

// Raw Markdown ⇄ rendered document. Converting through toMarkdown()/setMarkdown()
// round-trips cleanly for Markdown content and carries edits across the switch,
// so the toggle is only offered for .md files — running a .note with images and
// tables through Markdown would quietly drop them.
void DocumentView::setMarkdownSourceMode(bool raw)
{
    if (raw == m_mdSourceMode)
        return;
    m_mdSourceMode = raw;

    QTextDocument *doc = m_editor->document();
    const bool wasModified = doc->isModified();
    if (raw) {
        const QString md = doc->toMarkdown();
        doc->setPlainText(md);
        m_baseFontFamily = QStringLiteral("JetBrains Mono");
    } else {
        const QString md = doc->toPlainText();
        doc->setMarkdown(md);
        m_baseFontFamily = FontLibrary::defaultFamily().isEmpty()
                               ? doc->defaultFont().family()
                               : FontLibrary::defaultFamily();
    }
    applyBaseFont();

    m_editor->documentReset();
    doc->setModified(wasModified);      // a view switch is not an edit
    emit statusMessage(raw ? tr("Markdown source") : tr("Formatted view"), 1500);
}

// ---------------------------------------------------------------- file i/o

bool DocumentView::load(const QString &path, QString *errorOut)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("note"))
        return loadNote(path, errorOut);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorOut)
            *errorOut = tr("Cannot read %1:\n%2").arg(path, file.errorString());
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

    applySyntaxMode(suffix);
    m_editor->documentReset();
    m_editor->document()->setModified(false);
    return true;
}

bool DocumentView::save(const QString &path, QString *errorOut)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("note"))
        return saveNote(path, errorOut);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut)
            *errorOut = tr("Cannot write %1:\n%2").arg(path, file.errorString());
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
    return true;
}

bool DocumentView::saveNote(const QString &path, QString *errorOut)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut)
            *errorOut = tr("Cannot write %1:\n%2").arg(path, file.errorString());
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
    out << doc->toHtml();
    out << static_cast<quint32>(images.size());
    for (const auto &img : images)
        out << img.first << img.second;
    out << static_cast<quint32>(fonts.size());
    for (const auto &font : fonts)
        out << font.first << font.second;
    file.close();

    doc->setModified(false);
    return true;
}

bool DocumentView::loadNote(const QString &path, QString *errorOut)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorOut)
            *errorOut = tr("Cannot read %1:\n%2").arg(path, file.errorString());
        return false;
    }
    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_6_5);

    QByteArray magic;
    quint32 version = 0;
    in >> magic >> version;
    if (magic != QByteArray(kNoteMagic)) {
        if (errorOut)
            *errorOut = tr("%1 is not a valid Notepad note.").arg(QFileInfo(path).fileName());
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
    return true;
}

// True if the document carries formatting that plain text / Markdown can't keep
// (tables, images, bold/italic/underline, colors, non-default fonts or sizes).
bool DocumentView::hasRichFormatting() const
{
    const QTextDocument *doc = m_editor->document();
    if (!doc->rootFrame()->childFrames().isEmpty())
        return true;   // tables or floating images create child frames
    const QFont base = doc->defaultFont();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        if (b.blockFormat().headingLevel() > 0 || b.textList())
            return true;
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid())
                continue;
            const QTextCharFormat cf = frag.charFormat();
            if (cf.isImageFormat())
                return true;
            if (cf.fontWeight() > QFont::Normal || cf.fontItalic()
                || cf.fontUnderline() || cf.fontStrikeOut())
                return true;
            if (cf.hasProperty(QTextFormat::ForegroundBrush)
                && cf.foreground().color() != QColor(Qt::black))
                return true;
            if (cf.hasProperty(QTextFormat::BackgroundBrush))
                return true;
            if (cf.hasProperty(QTextFormat::FontPointSize)
                && qAbs(cf.fontPointSize() - base.pointSizeF()) > 0.1)
                return true;
            if (cf.hasProperty(QTextFormat::FontFamilies)) {
                const QStringList fams = cf.property(QTextFormat::FontFamilies).toStringList();
                if (!fams.isEmpty() && fams.constFirst() != base.family())
                    return true;
            }
        }
    }
    return false;
}
