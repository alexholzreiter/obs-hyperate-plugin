# OBS HypeRate Plugin

Native heart-rate-powered effects for OBS Studio.

OBS HypeRate turns your real heart rate (via [HypeRate](https://hyperate.io)) into
native OBS sources and filters — heartbeat camera shake, a pulsing glow, an on-stream
BPM display and threshold-based actions. Everything renders directly inside OBS — no
browser sources or extra overlays required.

Available for **Windows, macOS and Linux**. Licensed under **GPLv2**.

## How it works

1. Add the **HypeRate Heart Rate Input** source to your scene once.
2. Enter the HypeRate ID shown in your HypeRate app and click **Connect**.
3. Add HypeRate filters to any source, and/or add the BPM Display and Actions sources.
4. The filters and sources react to your live, smoothed BPM in real time — frame by frame.

A single shared heart-rate state is filled by the input source and read by every
filter/source, so you only connect once no matter how many effects you use.

## Sources & Filters

### Sources

- **HypeRate Heart Rate Input** (`hyperate_heart_rate_input`)
  - Non-visual control source that owns the HypeRate connection.
  - Enter your HypeRate ID, then Connect / Disconnect.
  - Adjustable smoothing (Direct / Balanced / Smooth / Very smooth).
  - Shows live connection status and the installed plugin version.
- **HypeRate BPM Display** (`hyperate_bpm_display`)
  - A visual, on-stream readout of your live BPM — no browser source, no lag.
  - Built-in segmented display or system-font rendering.
  - Digit styles (Block / Bold / Digital / Slim), custom font, width/height.
  - Static color or automatic **zone colors** (Resting / Active / High / Peak), optional glow.
  - Optional text outline with configurable color and thickness.
  - Option to keep showing while disconnected.
- **HypeRate Actions** (`hyperate_threshold_toggle`)
  - Fires an action when your BPM crosses a configurable threshold.
  - Show or hide a source, enable or disable a filter, switch scenes, or play a
    heartbeat sound when your heart rate goes above the threshold.
  - Configurable hysteresis to avoid flicker around the threshold.

### Filters

- **Heartbeat** — Camera Shake (`hyperate_heartbeat_camera_shake`)
  - Pulses / shakes the filtered source with every heartbeat.
  - Motion styles: Shake, Bounce, Pulse Classic, Beat Double.
  - Presets (Subtle / Normal / Intense / Custom), threshold & max BPM mapping,
    max shake in pixels, motion smoothing, heartbeat decay, overscan, test mode.
- **Heartbeat Glow** (`hyperate_heartbeat_glow`)
  - A configurable-color glow that pulses with each beat.
  - Pulse styles: Classic, Soft, Double Beat.
  - Presets, opacity, threshold & max BPM mapping, scale, source boost, tint
    strength, test mode.

Every filter has a **test mode** with a manual test BPM, so you can design and preview
effects without a live heart-rate connection.

## Heart rate zones

The shared state classifies BPM into four zones (used e.g. by BPM Display colors):

- Resting: below 100 BPM
- Active: 100–129 BPM
- High: 130–159 BPM
- Peak: 160+ BPM

It also tracks the session maximum BPM and applies exponential smoothing.

## Install

Download the latest build for your platform from the
[Releases page](https://github.com/alexholzreiter/obs-hyperate-plugin/releases), close
OBS, install, and restart OBS.

- **Windows** — installer (into your OBS Studio folder) or ZIP.
- **macOS** — installer (per-user, no admin password) or ZIP into
  `~/Library/Application Support/obs-studio/plugins/`.
- **Linux** — self-extracting `.run` installer or ZIP into
  `~/.config/obs-studio/plugins/`.

Full step-by-step instructions and per-platform paths are in
[`docs/release-and-installation.md`](docs/release-and-installation.md).

## Project layout

```text
buildspec.json          Plugin metadata, OBS dependency versions, macOS bundle id
CMakeLists.txt
CMakePresets.json
cmake/common/           Local template-compatible bootstrap/helper layer
data/
  effects/              Shader effects (e.g. heartbeat_glow.effect)
  locale/               en-US.ini, de-DE.ini
src/
  plugin-main.cpp       Registers all sources and filters
  heart_rate/           Thread-safe shared heart-rate state, smoothing, zones
  hyperate/             WebSocket client (libwebsockets) and HypeRate protocol
  sources/              Heart Rate Input, BPM Display, Actions
  filters/              Heartbeat Camera Shake, Heartbeat Glow
  util/
docs/                   Build & release documentation
scripts/                Local macOS libobs setup helpers
```

## Build from source

The project follows the current OBS plugin-template style:

- `buildspec.json` holds plugin metadata and OBS dependency versions.
- `CMakePresets.json` provides platform presets.
- `cmake/common/` contains a small local template-compatible bootstrap/helper layer.

You need CMake plus OBS/libobs development dependencies. If CMake cannot find `libobs`,
read [`docs/macos-libobs.md`](docs/macos-libobs.md) — installing the OBS app alone does
not expose the native plugin development files.

For a local macOS development setup, the shortest path is:

```sh
bash scripts/setup-obs-dev-macos.sh
bash scripts/build-local-libobs-macos.sh
```

The second script prints the `libobs_DIR` value to pass into the configure command.

### macOS

```sh
brew install cmake ninja pkg-config libwebsockets

cmake --preset macos-ninja \
  -DCMAKE_PREFIX_PATH="/path/to/obs-studio/build;/path/to/obs-deps"
cmake --build --preset macos-ninja
```

The plugin target is `obs-hyperate.plugin` on macOS. For an Xcode universal build use
the `macos` preset instead.

### Windows

Install Visual Studio 2022 (C++), CMake, an OBS development environment, and
`libwebsockets` (via vcpkg or your OBS dependency bundle). Then:

```powershell
cmake --preset windows-x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DCMAKE_PREFIX_PATH=C:\path\to\obs-build
cmake --build --preset windows-x64
```

### Linux

```sh
sudo apt install cmake ninja-build pkg-config libobs-dev libwebsockets-dev

cmake --preset ubuntu-x86_64
cmake --build --preset ubuntu-x86_64
sudo cmake --install build_x86_64
```

## HypeRate connection & API token

All protocol assumptions live in
[`src/hyperate/hyperate_client.cpp`](src/hyperate/hyperate_client.cpp). The client
follows the public HypeRate WebSocket reference:

- Connection URL: `wss://app.hyperate.io/ws/<device_id>?token=<api-key>`
- Join frame: `{"topic":"hr:<channel_id>","event":"phx_join","payload":{},"ref":"1"}`
- Keep-alive `ping` every 15 seconds
- BPM payload: `{"event":"hr_update","payload":{"hr":79},...}` (the parser also accepts
  `bpm`, `heartRate` and `heart_rate` for older examples)

So that streamers never have to paste an API key into OBS, a shared, limited HypeRate
token is compiled into the plugin. Builds can override it at configure time with
`-DHYPERATE_API_TOKEN=...`, and the release workflow injects it from the
`HYPERATE_API_TOKEN` repository secret when one is set.

## License

Licensed under the GNU General Public License v2.0 — see [`LICENSE`](LICENSE). This
matches the OBS Studio ecosystem, which is also GPLv2.

## Scope

The plugin stays focused on doing one thing well: native, real-time heart-rate
rendering and simple heart-rate-driven actions inside OBS. It works happily alongside
your existing automation and overlay tools.
