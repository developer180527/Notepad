#ifndef SPEECH_H
#define SPEECH_H

#include <QString>

// Speaking a word aloud, for the "Pronounce" action.
//
// Qt already abstracts the platform engines (AVSpeechSynthesizer on macOS, SAPI
// on Windows, speech-dispatcher/flite on Linux), so there is no per-platform
// code here — only a lazily created synthesiser, so the module costs nothing
// until the user actually asks for pronunciation.
namespace Speech {

bool isAvailable();
void say(const QString &text);

} // namespace Speech

#endif // SPEECH_H
