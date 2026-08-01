#include "document/documentexport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>

namespace {

// One logical piece of the document, in reading order.
struct Node
{
    QString type;              // heading | paragraph | list | table | image | code
    QString text;
    int level = 0;             // heading level
    QString alignment;
    QStringList items;         // list entries
    QList<QStringList> rows;   // table cells
    QString name;              // image resource name
};

QString alignmentName(Qt::Alignment a)
{
    if (a & Qt::AlignHCenter) return QStringLiteral("center");
    if (a & Qt::AlignRight)   return QStringLiteral("right");
    if (a & Qt::AlignJustify) return QStringLiteral("justify");
    return QStringLiteral("left");
}

QString imageNameIn(const QTextBlock &block)
{
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment frag = it.fragment();
        if (frag.isValid() && frag.charFormat().isImageFormat())
            return frag.charFormat().toImageFormat().name();
    }
    return QString();
}

QString tableCellText(const QTextTableCell &cell)
{
    QStringList parts;
    for (auto it = cell.begin(); !it.atEnd(); ++it)
        if (const QTextBlock b = it.currentBlock(); b.isValid() && !b.text().isEmpty())
            parts << b.text();
    return parts.join(QLatin1Char(' '));
}

// Walk the document once, collapsing consecutive list items into one node and
// pulling whole tables out of the block stream.
QList<Node> outline(const QTextDocument *doc)
{
    QList<Node> nodes;
    QSet<QTextTable *> emittedTables;

    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        QTextCursor probe(const_cast<QTextDocument *>(doc));
        probe.setPosition(block.position());

        if (QTextTable *table = probe.currentTable()) {
            if (emittedTables.contains(table))
                continue;                       // already captured whole
            emittedTables.insert(table);
            Node n;
            n.type = QStringLiteral("table");
            for (int r = 0; r < table->rows(); ++r) {
                QStringList row;
                for (int c = 0; c < table->columns(); ++c)
                    row << tableCellText(table->cellAt(r, c));
                n.rows << row;
            }
            nodes << n;
            continue;
        }

        const QString name = imageNameIn(block);
        if (!name.isEmpty()) {
            Node n;
            n.type = QStringLiteral("image");
            n.name = name;
            nodes << n;
            continue;
        }

        const QString text = block.text();
        if (text.trimmed().isEmpty())
            continue;                           // blank spacing blocks

        if (QTextList *list = block.textList()) {
            // Append to the previous list node when this block continues it.
            if (!nodes.isEmpty() && nodes.last().type == QStringLiteral("list")
                && nodes.last().level == list->format().indent()) {
                nodes.last().items << text;
            } else {
                Node n;
                n.type = QStringLiteral("list");
                n.level = list->format().indent();
                n.items << text;
                nodes << n;
            }
            continue;
        }

        Node n;
        n.text = text;
        n.alignment = alignmentName(block.blockFormat().alignment());
        const int heading = block.blockFormat().headingLevel();
        if (heading > 0) {
            n.type = QStringLiteral("heading");
            n.level = heading;
        } else if (block.blockFormat().nonBreakableLines()) {
            n.type = QStringLiteral("code");
        } else {
            n.type = QStringLiteral("paragraph");
        }
        nodes << n;
    }
    return nodes;
}

QJsonObject nodeToJson(const Node &n)
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), n.type);
    if (n.type == QStringLiteral("table")) {
        QJsonArray rows;
        for (const QStringList &row : n.rows) {
            QJsonArray cells;
            for (const QString &c : row)
                cells.append(c);
            rows.append(cells);
        }
        o.insert(QStringLiteral("rows"), rows);
    } else if (n.type == QStringLiteral("list")) {
        QJsonArray items;
        for (const QString &i : n.items)
            items.append(i);
        o.insert(QStringLiteral("items"), items);
        if (n.level > 0)
            o.insert(QStringLiteral("indent"), n.level);
    } else if (n.type == QStringLiteral("image")) {
        o.insert(QStringLiteral("name"), n.name);
    } else {
        o.insert(QStringLiteral("text"), n.text);
        if (n.level > 0)
            o.insert(QStringLiteral("level"), n.level);
        if (n.alignment != QStringLiteral("left"))
            o.insert(QStringLiteral("alignment"), n.alignment);
    }
    return o;
}

// Always double-quote and escape: cheap, and safe for text containing colons,
// leading dashes, #, or anything else YAML would otherwise reinterpret.
QString yamlScalar(const QString &in)
{
    QString s = in;
    s.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    s.replace(QLatin1Char('"'), QLatin1String("\\\""));
    s.replace(QLatin1Char('\n'), QLatin1String("\\n"));
    s.replace(QLatin1Char('\t'), QLatin1String("\\t"));
    return QLatin1Char('"') + s + QLatin1Char('"');
}

QByteArray toYaml(const QList<Node> &nodes)
{
    QString out = QStringLiteral("blocks:\n");
    if (nodes.isEmpty())
        out = QStringLiteral("blocks: []\n");
    for (const Node &n : nodes) {
        out += QStringLiteral("  - type: %1\n").arg(n.type);
        if (n.type == QStringLiteral("table")) {
            out += QStringLiteral("    rows:\n");
            for (const QStringList &row : n.rows) {
                out += QStringLiteral("      -");
                if (row.isEmpty())
                    out += QStringLiteral(" []");
                out += QLatin1Char('\n');
                for (const QString &c : row)
                    out += QStringLiteral("        - %1\n").arg(yamlScalar(c));
            }
        } else if (n.type == QStringLiteral("list")) {
            if (n.level > 0)
                out += QStringLiteral("    indent: %1\n").arg(n.level);
            out += QStringLiteral("    items:\n");
            for (const QString &i : n.items)
                out += QStringLiteral("      - %1\n").arg(yamlScalar(i));
        } else if (n.type == QStringLiteral("image")) {
            out += QStringLiteral("    name: %1\n").arg(yamlScalar(n.name));
        } else {
            out += QStringLiteral("    text: %1\n").arg(yamlScalar(n.text));
            if (n.level > 0)
                out += QStringLiteral("    level: %1\n").arg(n.level);
            if (n.alignment != QStringLiteral("left"))
                out += QStringLiteral("    alignment: %1\n").arg(n.alignment);
        }
    }
    return out.toUtf8();
}

} // namespace

namespace DocumentExport {

QList<FormatInfo> formats()
{
    return {
        {Format::Note,      QT_TRANSLATE_NOOP("DocumentExport", "Notepad Note"), "note", true},
        {Format::Markdown,  QT_TRANSLATE_NOOP("DocumentExport", "Markdown"),     "md",   false},
        {Format::Html,      QT_TRANSLATE_NOOP("DocumentExport", "HTML"),         "html", true},
        {Format::PlainText, QT_TRANSLATE_NOOP("DocumentExport", "Plain Text"),   "txt",  false},
        {Format::Json,      QT_TRANSLATE_NOOP("DocumentExport", "JSON"),         "json", false},
        {Format::Yaml,      QT_TRANSLATE_NOOP("DocumentExport", "YAML"),         "yaml", false},
    };
}

Format formatForSuffix(const QString &suffix)
{
    const QString s = suffix.toLower();
    if (s == QLatin1String("note")) return Format::Note;
    if (s == QLatin1String("md") || s == QLatin1String("markdown")) return Format::Markdown;
    if (s == QLatin1String("html") || s == QLatin1String("htm")) return Format::Html;
    if (s == QLatin1String("json")) return Format::Json;
    if (s == QLatin1String("yaml") || s == QLatin1String("yml")) return Format::Yaml;
    return Format::PlainText;
}

QString suffixFor(Format format)
{
    for (const FormatInfo &f : formats())
        if (f.format == format)
            return QString::fromLatin1(f.suffix);
    return QStringLiteral("txt");
}

QByteArray serialise(const QTextDocument *doc, Format format)
{
    switch (format) {
    case Format::Markdown:
        return const_cast<QTextDocument *>(doc)->toMarkdown().toUtf8();
    case Format::Html:
        return const_cast<QTextDocument *>(doc)->toHtml().toUtf8();
    case Format::Json: {
        QJsonArray blocks;
        for (const Node &n : outline(doc))
            blocks.append(nodeToJson(n));
        QJsonObject root;
        root.insert(QStringLiteral("format"), QStringLiteral("notepad-document"));
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("blocks"), blocks);
        return QJsonDocument(root).toJson(QJsonDocument::Indented);
    }
    case Format::Yaml:
        return toYaml(outline(doc));
    case Format::Note:        // written by DocumentView (needs the page preview)
    case Format::PlainText:
        break;
    }
    return doc->toPlainText().toUtf8();
}

} // namespace DocumentExport
