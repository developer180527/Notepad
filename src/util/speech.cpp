#include "util/speech.h"

#include <QTextToSpeech>

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

namespace Speech {

bool isAvailable()
{
    return synth()->state() != QTextToSpeech::Error;
}

void say(const QString &text)
{
    if (text.isEmpty() || !isAvailable())
        return;
    synth()->stop();               // cut off the previous word rather than queueing
    synth()->say(text);
}

} // namespace Speech
