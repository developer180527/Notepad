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
