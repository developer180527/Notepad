#ifndef DOCUMENTEXPORT_H
#define DOCUMENTEXPORT_H

#include <QByteArray>
#include <QString>
#include <QStringList>

class QTextDocument;

// Converting a document to the formats Notepad can write.
//
// Markdown/HTML/plain text come straight from QTextDocument. JSON and YAML do
// not: dumping toPlainText() into a .json file would not even be valid JSON, so
// those walk the document and emit a structured outline (headings, paragraphs,
// lists, tables, images) that another tool can actually consume.
namespace DocumentExport {

enum class Format { Note, Markdown, Html, PlainText, Json, Yaml };

// The formats offered in the converter, in menu order.
struct FormatInfo {
    Format format;
    const char *label;       // shown to the user
    const char *suffix;      // canonical file extension, no dot
    bool lossless;           // keeps colours/fonts/images
};
QList<FormatInfo> formats();

Format formatForSuffix(const QString &suffix);
QString suffixFor(Format format);

// Serialise the document. Note is handled by DocumentView (it needs the
// editor's page preview), so this covers everything else.
QByteArray serialise(const QTextDocument *doc, Format format);

} // namespace DocumentExport

#endif // DOCUMENTEXPORT_H
