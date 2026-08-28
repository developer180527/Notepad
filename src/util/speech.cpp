#include "util/speech.h"

#if defined(NOTEPAD_HAVE_TTS)
#  include <QTextToSpeech>
#endif

#if defined(NOTEPAD_HAVE_TTS)
namespace {

// Created on first use: constructing a synthesiser spins up a platform engine,
// which is not worth doing during startup for a feature most sessions never use.
QTextToSpeech *synth()
{
    static QTextToSpeech *instance = [] {
        auto *tts = new QTextToSpeech;
        tts->setRate(-0.2);        // slightly slow: this is for hearing a word clearly
        return tts;
    }();
    return instance;
}

} // namespace
#endif // NOTEPAD_HAVE_TTS

namespace Speech {

bool isAvailable()
{
#if defined(NOTEPAD_HAVE_TTS)
    return synth()->state() != QTextToSpeech::Error;
#else
    return false;
#endif
}

void say(const QString &text)
{
#if defined(NOTEPAD_HAVE_TTS)
    if (text.isEmpty() || !isAvailable())
        return;
    synth()->stop();               // cut off the previous word rather than queueing
    synth()->say(text);
#else
    Q_UNUSED(text);
#endif
}

} // namespace Speech
