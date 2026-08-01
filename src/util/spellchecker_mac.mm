#include "util/spellchecker.h"

#import <AppKit/AppKit.h>

// macOS backend. NSSpellChecker is the same engine the rest of the system uses,
// so the user's own learned words and language settings apply here too — there
// is nothing to bundle and no dictionary to ship.
namespace {

QString fromNS(NSString *s)
{
    return s ? QString::fromUtf8([s UTF8String]) : QString();
}

NSString *toNS(const QString &s)
{
    return [NSString stringWithUTF8String:s.toUtf8().constData()];
}

class MacSpellChecker final : public SpellChecker
{
public:
    MacSpellChecker()
    {
        // A document tag scopes "ignore" to this app rather than the whole system.
        m_tag = [NSSpellChecker uniqueSpellDocumentTag];
    }

    ~MacSpellChecker() override
    {
        [[NSSpellChecker sharedSpellChecker] closeSpellDocumentWithTag:m_tag];
    }

    bool isAvailable() const override { return true; }

    bool isCorrect(const QString &word) const override
    {
        if (!spellCheckWorthChecking(word))
            return true;
        NSString *s = toNS(word);
        const NSRange bad = [[NSSpellChecker sharedSpellChecker]
            checkSpellingOfString:s
                       startingAt:0
                         language:m_language.isEmpty() ? nil : toNS(m_language)
                             wrap:NO
           inSpellDocumentWithTag:m_tag
                        wordCount:nullptr];
        return bad.location == NSNotFound;
    }

    QStringList suggestions(const QString &word) const override
    {
        NSString *s = toNS(word);
        NSArray<NSString *> *guesses = [[NSSpellChecker sharedSpellChecker]
            guessesForWordRange:NSMakeRange(0, [s length])
                       inString:s
                       language:m_language.isEmpty()
                                    ? [[NSSpellChecker sharedSpellChecker] language]
                                    : toNS(m_language)
         inSpellDocumentWithTag:m_tag];
        QStringList out;
        for (NSString *g in guesses)
            out << fromNS(g);
        return out;
    }

    void learn(const QString &word) override
    {
        [[NSSpellChecker sharedSpellChecker] learnWord:toNS(word)];
    }

    void ignore(const QString &word) override
    {
        [[NSSpellChecker sharedSpellChecker] ignoreWord:toNS(word) inSpellDocumentWithTag:m_tag];
    }

    QString language() const override
    {
        return m_language.isEmpty() ? fromNS([[NSSpellChecker sharedSpellChecker] language])
                                    : m_language;
    }

    void setLanguage(const QString &language) override
    {
        m_language = language;
        if (!language.isEmpty())
            [[NSSpellChecker sharedSpellChecker] setLanguage:toNS(language)];
    }

    QStringList availableLanguages() const override
    {
        QStringList out;
        for (NSString *l in [[NSSpellChecker sharedSpellChecker] availableLanguages])
            out << fromNS(l);
        return out;
    }

private:
    NSInteger m_tag = 0;
    QString m_language;
};

} // namespace

SpellChecker *createMacSpellChecker()
{
    return new MacSpellChecker;
}
