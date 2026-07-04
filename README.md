# Framelit

A fast, always-on-top screen-region recorder for macOS and Linux. Frame a region, hit record, and get a clean GIF or MP4 — with inline preview and edit. Built in C++/Qt6 to feel like a utility, not an editor.

## Features

- **Region capture** — draggable, resizable, always-on-top capture window.
- **GIF / MP4 output** — palette-quantized GIF (giflib) or CRF-quality MP4 (Qt Multimedia).
- **Inline preview & edit** — trim in/out points and review takes without leaving the app.
- **Follow-mouse pan** — the capture region can track the cursor while recording.
- **Global hotkeys** — grow/shrink and follow-mouse toggle without focusing the app.
- **HiDPI, letterbox, and optional mic audio** for MP4.

## Install

Download the latest `Framelit.dmg` from Releases, drag **Framelit** to Applications, and launch.

On first launch macOS will prompt for **Screen Recording** and **Accessibility** permissions
(System Settings → Privacy & Security). Both are required — screen recording for capture,
accessibility for global hotkeys.

## Build from source

```sh
cmake -B build && cmake --build build
```

Requirements:
- Qt ≥ 6.5 (the repo pins convenience paths for 6.11.0 in `CMakeLists.txt`)
- CMake ≥ 3.22, a C++20 compiler
- **giflib** — auto-detected from Homebrew (`brew install giflib`) or fetched/built via CMake `FetchContent`
- **Linux only:** `libgif-dev`, plus `libxext-dev`/`libx11-dev` (real click-through via XShape)

Run: `open build/Framelit.app` (macOS) · `./build/framelit` (Linux)

Run tests: `ctest --test-dir build --output-on-failure`

### macOS code signing

The signing identity defaults to a developer certificate but is overridable at configure time:

```sh
# Ad-hoc (local, no notarization):
cmake -B build -DFRAMELIT_CODESIGN_IDENTITY=-

# Your Developer ID (or set FRAMELIT_CODESIGN_IDENTITY in the environment):
cmake -B build -DFRAMELIT_CODESIGN_IDENTITY="Developer ID Application: …"
```

## Global Hotkeys (macOS)

Requires Accessibility permission (System Settings → Privacy & Security → Accessibility).

| Shortcut | Action |
|---|---|
| `Cmd+Shift+=` | Grow capture region |
| `Cmd+Shift+-` | Shrink capture region |
| `Cmd+Shift+F` | Toggle follow-mouse |

## Reset saved settings

Framelit stores preferences (capture rect, output dir, format) via `QSettings` under the
`Kushview` / `Framelit` domain:

```sh
# macOS
defaults delete net.kushview.Framelit
```

## Roadmap

Framelit's arc is **capture → render → record → sync**. Capture, render, and record ship
today. **Sync** — cloud upload and shareable links — is planned and not yet implemented.

## License

Copyright © Kushview. See repository for license details.
