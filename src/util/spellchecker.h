#ifndef SPELLCHECKER_H
#define SPELLCHECKER_H

#include <QString>
#include <QStringList>

// Spell checking, abstracted because there is no portable API for it.
//
//   macOS    NSSpellChecker      — system-wide, every installed language
//   Windows  ISpellChecker (COM) — system-wide, Windows 8 and later
//   Linux    Enchant             — optional; wraps hunspell/aspell
//
// Anything without a backend gets the null implementation, where isAvailable()
// is false and every word is "correct". Callers must treat the feature as
// optional and hide their UI when it is unavailable, rather than assuming a
// checker exists.
class SpellChecker
{
public:
    virtual ~SpellChecker() = default;

    // The backend for this platform, or a null checker. Never returns nullptr.
    // Owned by the caller.
    static SpellChecker *create();

    virtual bool isAvailable() const { return false; }

    // False only when the word is genuinely misspelled. Numbers, URLs and
    // anything the backend cannot judge come back true so we never underline
    // something we are unsure about.
    virtual bool isCorrect(const QString &word) const { Q_UNUSED(word); return true; }
    virtual QStringList suggestions(const QString &word) const { Q_UNUSED(word); return {}; }

    // Add to the user's personal dictionary (persists across sessions).
    virtual void learn(const QString &word) { Q_UNUSED(word); }
    // Accept for this session only.
    virtual void ignore(const QString &word) { Q_UNUSED(word); }

    virtual QString language() const { return {}; }
    virtual void setLanguage(const QString &language) { Q_UNUSED(language); }
    virtual QStringList availableLanguages() const { return {}; }
};

// Shared filter: is this token even prose? Numbers, URLs, e-mail addresses,
// identifiers and ALLCAPS acronyms are excluded, so backends agree on what to
// underline and we never mark something we cannot judge.
bool spellCheckWorthChecking(const QString &word);

#endif // SPELLCHECKER_H
