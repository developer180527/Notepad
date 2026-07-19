#include "codehighlighter.h"

#include <QTextDocument>

namespace {

// Colors chosen to read well on the white page sheet.
const QColor kKey     (0x0b, 0x5c, 0xad);   // blue    — object keys / yaml keys
const QColor kString  (0xa3, 0x1d, 0x11);   // rust    — quoted strings
const QColor kNumber  (0x1c, 0x6b, 0x30);   // green   — numbers
const QColor kLiteral (0x7a, 0x3e, 0x9d);   // purple  — true/false/null
const QColor kComment (0x6a, 0x73, 0x7d);   // grey    — yaml comments
const QColor kPunct   (0x50, 0x50, 0x50);   // dark grey — braces/brackets

QTextCharFormat fmtOf(const QColor &c, bool italic = false, bool bold = false)
{
    QTextCharFormat f;
    f.setForeground(c);
    if (italic) f.setFontItalic(true);
    if (bold)   f.setFontWeight(QFont::DemiBold);
    return f;
}

} // namespace

CodeHighlighter::CodeHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
}

CodeHighlighter::Language CodeHighlighter::languageForSuffix(const QString &suffix)
{
    const QString s = suffix.toLower();
    if (s == QLatin1String("json"))
        return Language::Json;
    if (s == QLatin1String("yaml") || s == QLatin1String("yml"))
        return Language::Yaml;
    return Language::None;
}

void CodeHighlighter::setLanguage(Language lang)
{
    if (m_lang == lang)
        return;
    m_lang = lang;
    rebuildRules();
    rehighlight();
}

void CodeHighlighter::rebuildRules()
{
    m_rules.clear();
    if (m_lang == Language::None)
        return;

    // Rules are applied in order and later ones repaint earlier ones, so the
    // generic string rule must come *before* the key rule — otherwise it would
    // recolour quoted keys back to the string colour.
    if (m_lang == Language::Json) {
        m_rules.append({QRegularExpression(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"")),
                        fmtOf(kString), 0});
        m_rules.append({QRegularExpression(QStringLiteral("\\b-?\\d+(?:\\.\\d+)?(?:[eE][+-]?\\d+)?\\b")),
                        fmtOf(kNumber), 0});
        m_rules.append({QRegularExpression(QStringLiteral("\\b(?:true|false|null)\\b")),
                        fmtOf(kLiteral, false, true), 0});
        m_rules.append({QRegularExpression(QStringLiteral("[\\{\\}\\[\\]]")),
                        fmtOf(kPunct), 0});
        // "key":  — capture just the quoted key, not the colon. Last so it wins.
        m_rules.append({QRegularExpression(QStringLiteral("(\"(?:[^\"\\\\]|\\\\.)*\")\\s*:")),
                        fmtOf(kKey, false, true), 1});
        return;
    }

    // YAML
    m_rules.append({QRegularExpression(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"|'(?:[^']|'')*'")),
                    fmtOf(kString), 0});
    m_rules.append({QRegularExpression(QStringLiteral("\\b-?\\d+(?:\\.\\d+)?\\b")),
                    fmtOf(kNumber), 0});
    m_rules.append({QRegularExpression(
                        QStringLiteral("\\b(?:true|false|null|yes|no|on|off|~)\\b"),
                        QRegularExpression::CaseInsensitiveOption),
                    fmtOf(kLiteral, false, true), 0});
    // key:  at the start of a line (allowing indentation and list dashes).
    m_rules.append({QRegularExpression(QStringLiteral("^\\s*(?:-\\s*)?([\\w.$-]+)\\s*:")),
                    fmtOf(kKey, false, true), 1});
    // Comments last so they override anything matched inside them.
    m_rules.append({QRegularExpression(QStringLiteral("(?:^|\\s)#.*$")),
                    fmtOf(kComment, true), 0});
}

void CodeHighlighter::highlightBlock(const QString &text)
{
    if (m_lang == Language::None || m_rules.isEmpty())
        return;

    for (const Rule &rule : std::as_const(m_rules)) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const int start = m.capturedStart(rule.captureGroup);
            const int len = m.capturedLength(rule.captureGroup);
            if (start >= 0 && len > 0)
                setFormat(start, len, rule.format);
        }
    }
}
