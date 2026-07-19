#ifndef CODEHIGHLIGHTER_H
#define CODEHIGHLIGHTER_H

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QList>

// Lightweight JSON / YAML syntax highlighting for the page editor.
//
// Highlighter formats are applied by the layout as *additional* formats — they
// are never merged into the document's own char formats. That keeps saving
// clean: toPlainText()/toMarkdown() are unaffected, and the "would this lose
// formatting?" check in MainWindow doesn't mistake syntax colors for real
// user formatting.
class CodeHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    enum class Language { None, Json, Yaml };

    explicit CodeHighlighter(QTextDocument *parent = nullptr);

    void setLanguage(Language lang);
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

    Language m_lang = Language::None;
    QList<Rule> m_rules;
};

#endif // CODEHIGHLIGHTER_H
