# Packaging & file association (`.note`)

Notepad opens a `.note` passed on the command line or via the macOS open-file
event (`main.cpp`). To make a double-click in the file manager launch Notepad,
the OS must be told that Notepad owns the `.note` type. The runtime handling is
done; the OS registration differs per platform.

## macOS

The app bundle declares the `.note` document type and the `org.notepad.note`
UTI in `Info.plist.in` (wired up in `CMakeLists.txt`). Launch Services picks
this up when the app is in `/Applications` (or after it has been launched once).
If a freshly built bundle isn't associated yet, register it manually:

```sh
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister \
    -f build/Notepad.app
```

Then `open -a build/Notepad.app file.note` or a Finder double-click opens it.

## Windows

Associate the extension at install time (e.g. in your NSIS/WiX installer), or
for a quick local test via the registry:

```
HKEY_CURRENT_USER\Software\Classes\.note            (Default) = "Notepad.Document"
HKEY_CURRENT_USER\Software\Classes\Notepad.Document\shell\open\command
    (Default) = "C:\Path\To\Notepad.exe" "%1"
```

The `"%1"` is the file path, which `main.cpp` reads from `argv`.

## Linux

Install a MIME type and a `.desktop` entry:

`~/.local/share/mime/packages/notepad-note.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <mime-type type="application/x-notepad-note">
    <comment>Notepad Note</comment>
    <glob pattern="*.note"/>
  </mime-type>
</mime-info>
```

`~/.local/share/applications/notepad.desktop`:

```ini
[Desktop Entry]
Type=Application
Name=Notepad
Exec=/path/to/Notepad %f
MimeType=application/x-notepad-note;
```

Then:

```sh
update-mime-database ~/.local/share/mime
update-desktop-database ~/.local/share/applications
```
