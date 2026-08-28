#include "document/documenthighlighter.h"

#include <QTextBlock>
#include <QTextDocument>

#include "util/spellchecker.h"

#include <QRegularExpressionMatchIterator>

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

DocumentHighlighter::DocumentHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
}

DocumentHighlighter::Language DocumentHighlighter::languageForSuffix(const QString &suffix)
{
    const QString s = suffix.toLower();
    if (s == QLatin1String("json"))
        return Language::Json;
    if (s == QLatin1String("yaml") || s == QLatin1String("yml"))
        return Language::Yaml;
    return Language::None;
}

void DocumentHighlighter::setLanguage(Language lang)
{
    if (m_lang == lang)
        return;
    m_lang = lang;
    rebuildRules();
    rehighlight();
}

void DocumentHighlighter::rebuildRules()
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

void DocumentHighlighter::setSpellChecker(SpellChecker *checker)
{
    SpellChecker *usable = (checker && checker->isAvailable()) ? checker : nullptr;
    if (m_spell == usable)
        return;
    const bool hadSpelling = m_spell != nullptr;
    m_spell = usable;
    if (hadSpelling && !usable) {
        rehighlight();                 // turning it off must clear the underlines
        return;
    }
    // Turning it on only needs to cover what is currently on screen.
    const int from = m_spellFrom;
    const int to = m_spellTo;
    m_spellTo = -1;
    setSpellRange(from, to);
}

void DocumentHighlighter::setSpellRange(int from, int to)
{
    if (from == m_spellFrom && to == m_spellTo)
        return;
    m_spellFrom = from;
    m_spellTo = to;
    if (!spellCheckingActive() || to < from || !document())
        return;
    // Only the blocks that just came into range need re-examining; ones already
    // checked keep the formats they have.
    QTextBlock b = document()->findBlock(from);
    while (b.isValid() && b.position() <= to) {
        rehighlightBlock(b);
        b = b.next();
    }
}

bool DocumentHighlighter::spellCheckingActive() const
{
    return m_spell != nullptr && m_lang == Language::None;
}

// Underline misspelled words with the platform's own squiggle. Applied as an
// additional format, so it never becomes part of the document and cannot leak
// into a save or an export.
void DocumentHighlighter::markMisspellings(const QString &text)
{
    // Letters, plus the apostrophes that belong inside words ("don't", "O'Neill").
    static const QRegularExpression word(
        QStringLiteral("[\\p{L}]+(?:['\u2019][\\p{L}]+)*"));

    // WaveUnderline, not SpellCheckUnderline: the latter defers to QStyle to do
    // the drawing, and the page is rendered through QAbstractTextDocumentLayout
    // without a style context, so it comes out invisible. A wave is what the
    // platform style resolves to anyway — this just draws it unconditionally.
    QTextCharFormat fmt;
    fmt.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    fmt.setUnderlineColor(QColor(0xD0, 0x3A, 0x2E));

    auto it = word.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (!m_spell->isCorrect(m.captured()))
            setFormat(m.capturedStart(), m.capturedLength(), fmt);
    }
}

void DocumentHighlighter::highlightBlock(const QString &text)
{
    if (spellCheckingActive()) {
        // Outside the on-screen range, leave the block unmarked; it gets
        // checked when it scrolls into view.
        const int pos = currentBlock().position();
        if (m_spellTo >= m_spellFrom && pos + text.size() >= m_spellFrom && pos <= m_spellTo)
            markMisspellings(text);
        return;
    }
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
