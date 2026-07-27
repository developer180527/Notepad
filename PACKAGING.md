# Packaging: `.note` file association & thumbnails

Every v3 `.note` embeds a rendered preview PNG right after its version header.
All three platforms' thumbnailers extract that same PNG, so the integration is
mostly registration plumbing. The app already opens a `.note` passed on the
command line / open-file event (`main.cpp`), so the editing side of double-click
works everywhere once the OS routes the file to Notepad.

---

## macOS  (built & wired automatically)

Two app extensions in `mac/` are built, embedded in `Notepad.app/Contents/PlugIns/`,
code-signed and registered by the CMake `POST_BUILD` step (see `CMakeLists.txt`,
`if(APPLE)`):

- **Thumbnail** — `QLThumbnailProvider` → Finder icon preview.
- **Quick Look** — view-based `QLPreviewingController` → spacebar preview.

Just `cmake --build build`. If Finder is stale: `qlmanage -r && qlmanage -r cache`,
and keep `Notepad.app` at a stable path (e.g. `/Applications`). Override the
signing identity with `-DNOTEPAD_SIGN_IDENTITY="…"`.

---

## Linux

`cmake --build build` produces a `note-thumbnailer` binary; `cmake --install`
lays down the registration files:

| File | Installed to | Purpose |
|------|--------------|---------|
| `note-thumbnailer`        | `bin/`                | extracts + scales the embedded PNG |
| `linux/notepad.desktop`   | `share/applications/` | app + `.note` association (`Exec=Notepad %f`) |
| `linux/notepad-note.xml`  | `share/mime/packages/`| defines `application/x-notepad-note` (`*.note`) |
| `linux/notepad.thumbnailer` | `share/thumbnailers/` | tells GNOME/KDE/XFCE to run the thumbnailer |

After installing (to a prefix on `XDG_DATA_DIRS`, e.g. `/usr` or `~/.local`):

```sh
update-mime-database   "$PREFIX/share/mime"
update-desktop-database "$PREFIX/share/applications"
# thumbnail caches differ by DE; re-login or clear ~/.cache/thumbnails to refresh
```

`note-thumbnailer` and `Notepad` must be on `PATH` (or use absolute paths in the
`.desktop`/`.thumbnailer` `Exec=` lines). Verified manually:
`note-thumbnailer in.note out.png 256` writes the scaled preview.

---

## Windows

`cmake --build build` produces `NoteThumbnail.dll` (an `IThumbnailProvider` COM
server, `windows/NoteThumbnailProvider.cpp`). Keep it **next to `Notepad.exe`**.

**Registration is automatic**: on launch, `Notepad.exe` self-registers (per-user,
no admin) via `winregister.cpp` — it writes the `.note` → `Notepad.Note` ProgId +
open command, and points the `.note` thumbnail handler at `NoteThumbnail.dll`'s
CLSID. So running Notepad once enables both double-click open and thumbnails.

Manual alternative (if not self-registering):

```bat
regsvr32 NoteThumbnail.dll          REM registers the CLSID + .note handler (HKCU)
```

To refresh Explorer's thumbnail cache after first install, restart Explorer or
clear the thumbnail cache. An installer (WiX/NSIS) should drop `Notepad.exe` +
`NoteThumbnail.dll` together and may register machine-wide (HKLM) instead.

> Status: the Windows DLL + registration are written but were **not** compiled or
> tested on the authoring machine (macOS). They follow Microsoft's standard
> `IThumbnailProvider`/`IInitializeWithStream` pattern; build with MSVC + the
> Windows SDK and verify in Explorer.

---

## Making Notepad the default for `.txt` (and `.md`, `.html`)

Notepad now **registers as a handler** for these text types on all three
platforms, so it appears in "Open With". Becoming the *default* can't be forced
programmatically (Windows protects it via a UserChoice hash; macOS requires a
user action) — set it per OS:

- **macOS**: declared in `Info.plist.in` as an `Alternate` handler for
  `public.plain-text`, `public.html`, `net.daringfireball.markdown`. To default:
  Finder → select a `.txt` → ⌘I → "Open with" → Notepad → "Change All…".
- **Windows**: `winregister.cpp` adds `Notepad.Text` to each extension's
  `OpenWithProgids`. To default: right-click a `.txt` → "Open with" → "Choose
  another app" → Notepad → "Always".
- **Linux**: the `.desktop` lists `text/plain;text/markdown;text/html`. To
  default (per-user, scriptable in an installer):
  ```sh
  xdg-mime default notepad.desktop text/plain
  xdg-mime default notepad.desktop text/markdown
  xdg-mime default notepad.desktop text/html
  ```

---

# Signing & notarizing the macOS DMG

The release workflow always produces `Notepad-macos.dmg`. Whether that DMG opens
cleanly on someone else's Mac depends on which credentials are configured:

| Configured | Result |
| --- | --- |
| nothing | Ad-hoc signature. Works locally; other Macs show *"can't be opened because Apple cannot check it for malicious software"* and need a right-click → Open. |
| Developer ID secrets | Signed with your certificate and hardened runtime. Gatekeeper still warns until the app is notarized. |
| + notarization secrets | Signed, notarized and stapled. Opens with no warning, and works offline. |

## What you need from Apple

Notarization requires a **paid Apple Developer Program membership** — a free
account can only issue "Apple Development" certificates, which are for running
on your own machines and are rejected when distributed.

1. Join the Apple Developer Program.
2. In *Certificates, Identifiers & Profiles*, create a **Developer ID
   Application** certificate and download it.
3. Open it in Keychain Access, right-click → **Export** as a `.p12` with a
   password.
4. Create an **app-specific password** at <https://appleid.apple.com> →
   *Sign-In and Security* → *App-Specific Passwords* (your normal Apple ID
   password will not work for notarization).

## Repository secrets

Add these under *Settings → Secrets and variables → Actions*. Every one is
optional; the workflow degrades to the row above if any are missing.

| Secret | Value |
| --- | --- |
| `MACOS_CERT_P12` | The `.p12`, base64-encoded: `base64 -i cert.p12 \| pbcopy` |
| `MACOS_CERT_PASSWORD` | The password you set when exporting the `.p12` |
| `MACOS_SIGN_IDENTITY` | Exact identity name, e.g. `Developer ID Application: Your Name (ABCDE12345)` — check with `security find-identity -v -p codesigning` |
| `MACOS_TEAM_ID` | Your 10-character team id (the part in parentheses above) |
| `MACOS_NOTARY_APPLE_ID` | The Apple ID that owns the membership |
| `MACOS_NOTARY_PASSWORD` | The app-specific password from step 4 |

The certificate is imported into a temporary keychain that is deleted when the
job finishes, so it never persists on the runner.

## Why the Qt frameworks are re-signed

Hardened runtime (required for notarization) enables library validation, which
refuses to load nested code signed by a different team. `macdeployqt` copies in
Qt frameworks carrying whoever built them as their signer, so the workflow
re-signs every framework, dylib and app extension with *your* identity before
signing the app itself — inside-out, so each signature covers finished contents.

## Verifying a release DMG

```sh
codesign -dv --verbose=2 /Volumes/Notepad/Notepad.app   # identity + team id
xcrun stapler validate Notepad-macos.dmg                # notarization ticket
spctl --assess --type open --context context:primary-signature -vv Notepad-macos.dmg
```
