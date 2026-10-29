# Release and Installation

This project ships native OBS plugin builds as ZIP files and first-pass installers attached to GitHub Actions runs and draft GitHub Releases.

## Creating a Release

The GitHub Actions workflow in `.github/workflows/release.yml` runs in two ways:

- Manually from GitHub Actions: **Build and Release** -> **Run workflow**
- Automatically for tags starting with `v`, for example:

```sh
git tag v0.1.0
git push origin v0.1.0
```

For tag builds, the workflow creates a draft GitHub Release and attaches platform ZIP files plus installers.

## HypeRate API Token

Streamers should not need to enter an API token manually.

The plugin currently compiles a bundled HypeRate API token into private builds. The workflow also supports an optional repository secret:

```text
HYPERATE_API_TOKEN
```

If that secret is present, CI uses it at build time. If it is missing, the development fallback token in `CMakeLists.txt` is used.

For public distribution, use a dedicated public/plugin token with limited permissions, not a personal or admin token.

## User Installation

Users should close OBS before installing or replacing the plugin.

### macOS

Download:

```text
obs-hyperate-<version>-macos.zip
```

Unzip it and copy:

```text
obs-hyperate.plugin
```

to:

```text
~/Library/Application Support/obs-studio/plugins/
```

Then restart OBS.

Installer alternative:

```text
obs-hyperate-<version>-macos.pkg
```

This installs the plugin for the current user to:

```text
~/Library/Application Support/obs-studio/plugins/
```

The macOS packages bundle the required WebSocket/OpenSSL runtime libraries, so
users do not need Homebrew or `libwebsockets` installed.

OBS on macOS only loads plugins from this per-user location, not from the
system-wide `/Library` path, so the installer targets the current user's home
and does not require an administrator password.

### Windows

Download:

```text
obs-hyperate-<version>-windows-x64.zip
```

Unzip it. Copy the contained:

```text
obs-plugins
data
```

folders into your OBS Studio installation folder, usually:

```text
C:\Program Files\obs-studio\
```

The final layout should look like:

```text
C:\Program Files\obs-studio\obs-plugins\64bit\obs-hyperate.dll
C:\Program Files\obs-studio\data\obs-plugins\obs-hyperate\locale\de-DE.ini
```

Then restart OBS.

Installer alternative:

```text
obs-hyperate-<version>-windows-x64-installer.exe
```

This installs the plugin into the selected OBS Studio installation folder, usually:

```text
C:\Program Files\obs-studio\
```

It also removes the old pre-0.1.3 per-user install folder if it exists:

```text
%APPDATA%\obs-studio\plugins\obs-hyperate
```

### Linux

Download:

```text
obs-hyperate-<version>-linux-x86_64.zip
```

Unzip it. Copy the contained:

```text
obs-hyperate
```

folder to:

```text
~/.config/obs-studio/plugins/
```

The final layout should look like:

```text
~/.config/obs-studio/plugins/obs-hyperate/bin/64bit/obs-hyperate.so
~/.config/obs-studio/plugins/obs-hyperate/data/locale/de-DE.ini
```

Then restart OBS.

Installer alternative:

```text
chmod +x obs-hyperate-<version>-linux-x86_64-installer.run
./obs-hyperate-<version>-linux-x86_64-installer.run
```

This installs the plugin for the current Linux user to:

```text
~/.config/obs-studio/plugins/obs-hyperate
```

## OBS 33 plugin layout

OBS 33 introduced a new plugin-manager layout and labels plugins installed in the
old locations as "Legacy". The installers therefore write **both** layouts so the
plugin works on OBS 32 and older *and* is recognised as a modern plugin on OBS 33+
(OBS 33 loads the modern copy first and skips the legacy duplicate):

- **Windows:** legacy `…\obs-studio\obs-plugins\64bit\obs-hyperate.dll` **and** modern `C:\ProgramData\obs-studio\plugins\obs-hyperate\obs-hyperate.dll`.
- **Linux:** legacy `~/.config/obs-studio/plugins/obs-hyperate/bin/64bit/obs-hyperate.so` **and** modern `~/.local/share/obs-studio/plugins/obs-hyperate/obs-hyperate.so`.
- **macOS:** the `.plugin` bundle in `~/Library/Application Support/obs-studio/plugins/` is already the modern layout on all OBS versions — no change.

## Current Packaging Notes

- macOS builds are not signed or notarized yet.
- macOS PKG installers are unsigned and install per-user into `~/Library/Application Support/obs-studio/plugins/`, because OBS on macOS does not scan the system-wide `/Library` location.
- Windows builds are not code-signed yet.
- Windows EXE installers are unsigned and install into the selected OBS Studio installation folder. Administrator permission is normally required for `C:\Program Files\obs-studio`.
- Windows packages contain a single self-contained `obs-hyperate.dll`. libwebsockets, OpenSSL, zlib and libuv are linked statically, so no sibling runtime DLLs ship. Earlier versions bundled `websockets.dll`, `libssl-3-x64.dll`, `uv.dll`, `z.dll` and friends; because Windows loads only one DLL per file name per process, those generic names could collide with other OBS plugins and stop `obs-hyperate.dll` from loading. Leftover copies from an older install are inert and can be deleted.
- Linux `.run` installers are self-extracting shell installers and install per-user.
- Linux builds target Ubuntu 24.04 x86_64 first.
- The workflow is a first release pipeline and may need one or two CI iterations to match OBS runner dependency details exactly.
