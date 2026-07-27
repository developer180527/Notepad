#ifndef WINREGISTER_H
#define WINREGISTER_H

// Registers Notepad's .note file association and thumbnail handler on Windows
// (per-user under HKCU, no admin needed). No-op on macOS/Linux.
void registerWindowsIntegration();

#endif // WINREGISTER_H
