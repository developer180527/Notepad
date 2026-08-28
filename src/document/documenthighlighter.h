#ifndef DOCUMENTHIGHLIGHTER_H
#define DOCUMENTHIGHLIGHTER_H

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QList>

class SpellChecker;

// The document's one highlighter: JSON/YAML syntax colouring for data files,
// and misspelling underlines for prose.
//
// Both live here because Qt permits a single QSyntaxHighlighter per
// QTextDocument. They are naturally exclusive anyway — a JSON file is not prose
// — so setting a language turns syntax colouring on and spell checking off.
//
// Highlighter formats are applied by the layout as *additional* formats — they
// are never merged into the document's own char formats. That keeps saving
// clean: toPlainText()/toMarkdown() are unaffected, and the "would this lose
// formatting?" check in MainWindow doesn't mistake syntax colors for real
// user formatting.
class DocumentHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    enum class Language { None, Json, Yaml };

    explicit DocumentHighlighter(QTextDocument *parent = nullptr);

    void setLanguage(Language lang);

    // Spell checking is only applied when no code language is active. Passing
    // nullptr (or an unavailable checker) turns it off.
    void setSpellChecker(SpellChecker *checker);
    bool spellCheckingActive() const;

    // Spell checking is confined to a document range — in practice whatever is
    // on screen. Checking a whole document up front costs one dictionary call
    // per word before anything can be shown (8.8 s for a 1 MB Markdown file),
    // and almost all of it is for text the user cannot see. Blocks keep their
    // underlines once checked, so scrolling fills the document in gradually.
    void setSpellRange(int from, int to);
    Language language() const { return m_lang; }

    // Map a file suffix to a language (json / yaml / yml), else None.
    static Language languageForSuffix(const QString &suffix);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
        int captureGroup = 0;    // which group to paint (0 = whole match)
    };

    void rebuildRules();
    void markMisspellings(const QString &text);

    Language m_lang = Language::None;
    SpellChecker *m_spell = nullptr;   // not owned
    int m_spellFrom = 0;
    int m_spellTo = -1;                // -1 = nothing checked yet
    QList<Rule> m_rules;
};

#endif // DOCUMENTHIGHLIGHTER_H
