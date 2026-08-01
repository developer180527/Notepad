#include "util/spellchecker.h"

#include <QLocale>
#include <QRegularExpression>

#if defined(Q_OS_WIN)
#  include <windows.h>
#  include <spellcheck.h>
#  include <wrl/client.h>
#endif

#if defined(NOTEPAD_HAVE_ENCHANT)
#  include <enchant.h>
#endif

// Backends live in their own translation units where they need a different
// language (Objective-C++ on macOS).
#if defined(Q_OS_MACOS)
SpellChecker *createMacSpellChecker();
#endif

namespace {

// Shared by every backend: decide what is even worth checking. Numbers, URLs,
// file paths and code-ish tokens are not prose, and underlining them is noise.
bool worthChecking(const QString &word)
{
    if (word.size() < 2)
        return false;
    static const QRegularExpression skip(
        QStringLiteral("^(?:[\\d.,:%+-]+"                 // numbers, versions
                       "|[a-zA-Z]+://.*"                  // URLs
                       "|[^@\\s]+@[^@\\s]+\\.[^@\\s]+"    // e-mail
                       "|.*[_/\\\\].*"                    // snake_case, paths
                       "|.*\\d.*)$"));                    // anything with a digit
    if (skip.match(word).hasMatch())
        return false;
    // ALLCAPS is usually an acronym; mixedCase is usually an identifier.
    if (word == word.toUpper())
        return false;
    const QString rest = word.mid(1);
    if (rest != rest.toLower())
        return false;
    return true;
}

class NullSpellChecker final : public SpellChecker
{
};

#if defined(NOTEPAD_HAVE_ENCHANT)
// Linux: Enchant brokers whichever engine the user actually has (hunspell,
// aspell, nuspell), including their personal word list, so we neither ship
// dictionaries nor pick an engine for them.
class EnchantSpellChecker final : public SpellChecker
{
public:
    EnchantSpellChecker()
    {
        m_broker = enchant_broker_init();
        if (m_broker)
            setLanguage(QLocale::system().name());   // e.g. en_GB
    }

    ~EnchantSpellChecker() override
    {
        if (m_broker && m_dict)
            enchant_broker_free_dict(m_broker, m_dict);
        if (m_broker)
            enchant_broker_free(m_broker);
    }

    bool isAvailable() const override { return m_dict != nullptr; }

    bool isCorrect(const QString &word) const override
    {
        if (!m_dict || !worthChecking(word))
            return true;
        const QByteArray utf8 = word.toUtf8();
        return enchant_dict_check(m_dict, utf8.constData(), utf8.size()) == 0;
    }

    QStringList suggestions(const QString &word) const override
    {
        QStringList out;
        if (!m_dict)
            return out;
        const QByteArray utf8 = word.toUtf8();
        size_t count = 0;
        char **list = enchant_dict_suggest(m_dict, utf8.constData(), utf8.size(), &count);
        if (!list)
            return out;
        for (size_t i = 0; i < count && i < 10; ++i)
            out << QString::fromUtf8(list[i]);
        enchant_dict_free_string_list(m_dict, list);
        return out;
    }

    void learn(const QString &word) override
    {
        if (!m_dict)
            return;
        const QByteArray utf8 = word.toUtf8();
        enchant_dict_add(m_dict, utf8.constData(), utf8.size());
    }

    void ignore(const QString &word) override
    {
        if (!m_dict)
            return;
        const QByteArray utf8 = word.toUtf8();
        enchant_dict_add_to_session(m_dict, utf8.constData(), utf8.size());
    }

    QString language() const override { return m_language; }

    void setLanguage(const QString &language) override
    {
        if (!m_broker)
            return;
        EnchantDict *dict = enchant_broker_request_dict(m_broker, language.toUtf8().constData());
        if (!dict && language != QLatin1String("en_US"))
            dict = enchant_broker_request_dict(m_broker, "en_US");   // last resort
        if (!dict)
            return;
        if (m_dict)
            enchant_broker_free_dict(m_broker, m_dict);
        m_dict = dict;
        m_language = language;
    }

private:
    EnchantBroker *m_broker = nullptr;
    EnchantDict *m_dict = nullptr;
    QString m_language;
};
#endif // NOTEPAD_HAVE_ENCHANT

#if defined(Q_OS_WIN)
// Windows 8 and later ship ISpellChecker; on anything older the factory simply
// fails to create and we fall back to the null checker.
class WinSpellChecker final : public SpellChecker
{
public:
    WinSpellChecker()
    {
        if (FAILED(CoCreateInstance(__uuidof(SpellCheckerFactory), nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&m_factory))))
            return;
        setLanguage(QLocale::system().name().replace(QLatin1Char('_'), QLatin1Char('-')));
    }

    bool isAvailable() const override { return m_checker != nullptr; }

    bool isCorrect(const QString &word) const override
    {
        if (!m_checker || !worthChecking(word))
            return true;
        Microsoft::WRL::ComPtr<IEnumSpellingError> errors;
        if (FAILED(m_checker->Check(reinterpret_cast<LPCWSTR>(word.utf16()), &errors)))
            return true;
        Microsoft::WRL::ComPtr<ISpellingError> first;
        return errors->Next(&first) != S_OK;      // S_FALSE means "no errors"
    }

    QStringList suggestions(const QString &word) const override
    {
        QStringList out;
        if (!m_checker)
            return out;
        Microsoft::WRL::ComPtr<IEnumString> values;
        if (FAILED(m_checker->Suggest(reinterpret_cast<LPCWSTR>(word.utf16()), &values)))
            return out;
        LPOLESTR item = nullptr;
        while (values->Next(1, &item, nullptr) == S_OK && out.size() < 10) {
            out << QString::fromWCharArray(item);
            CoTaskMemFree(item);
        }
        return out;
    }

    void learn(const QString &word) override
    {
        if (m_checker)
            m_checker->Add(reinterpret_cast<LPCWSTR>(word.utf16()));
    }

    void ignore(const QString &word) override
    {
        if (m_checker)
            m_checker->Ignore(reinterpret_cast<LPCWSTR>(word.utf16()));
    }

    QString language() const override { return m_language; }

    void setLanguage(const QString &language) override
    {
        if (!m_factory)
            return;
        BOOL supported = FALSE;
        const std::wstring tag = language.toStdWString();
        if (FAILED(m_factory->IsSupported(tag.c_str(), &supported)) || !supported)
            return;
        Microsoft::WRL::ComPtr<ISpellChecker> checker;
        if (FAILED(m_factory->CreateSpellChecker(tag.c_str(), &checker)))
            return;
        m_checker = checker;
        m_language = language;
    }

private:
    Microsoft::WRL::ComPtr<ISpellCheckerFactory> m_factory;
    Microsoft::WRL::ComPtr<ISpellChecker> m_checker;
    QString m_language;
};
#endif // Q_OS_WIN

} // namespace

bool spellCheckWorthChecking(const QString &word)
{
    return worthChecking(word);
}

SpellChecker *SpellChecker::create()
{
#if defined(Q_OS_MACOS)
    return createMacSpellChecker();
#elif defined(Q_OS_WIN)
    auto *win = new WinSpellChecker;
    if (win->isAvailable())
        return win;
    delete win;
#elif defined(NOTEPAD_HAVE_ENCHANT)
    auto *enchant = new EnchantSpellChecker;
    if (enchant->isAvailable())
        return enchant;
    delete enchant;
#endif
    return new NullSpellChecker;
}
