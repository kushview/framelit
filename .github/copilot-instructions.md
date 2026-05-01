# Screen Capture Qt — Copilot Instructions

## Project

A lightweight always-on-top screen region recorder built in C++/Qt6. Inspired by LICEcap, modernized with inline preview, clipboard-first output, and a minimal UI. The capture window is the product — it should feel like a utility, not an editor.

## Principles

- **KISS**: Always prefer the simplest solution that works. No clever abstractions, no over-engineering.
- **DRY**: Don't duplicate logic. If something is written twice, it belongs in one place.
- Avoid adding features, refactoring, or "improvements" beyond what was asked.

## Language & Tooling

- C++20, Qt >= 6.5, CMake
- Build: `cmake -B build && cmake --build build`

## Conventions

- All code in `namespace sc {}`
- `main.cpp` is the only file outside the namespace
- Filenames: `alllowercase.hpp` / `alllowercase.cpp`
- Source layout: flat `src/`, only `src/ui/` as a subdirectory
- No `.h` — use `.hpp` for headers

## Architecture

Two always-on-top windows:

1. **CaptureWindow** — transparent frameless overlay; handles drag/resize; click-through while recording
2. **ControlBar** — dark opaque bar docked below the capture rect; polls `CaptureWindow::geometry()` via a 16ms `QTimer` to stay snapped (no signals needed)

`AppController` owns both windows and the state machine (`AppState` enum). Workers for recording and encoding run on `QThread`s and communicate back via signals.

## Compositing

`QGraphicsScene` is the compositor. All visual layers — screen frame, camera PIP, annotations — are `QGraphicsItem` subclasses in a shared scene. Qt handles z-order, transforms, opacity, and dirty tracking. Use `QGraphicsScene::render()` for CPU output (GIF). For future video output, prefer `QRhi` (Metal on macOS) to keep compositing on the GPU. Never roll a custom layer/compositor abstraction when `QGraphicsScene` already provides it.

## Key Types

```cpp
enum class AppState { Idle, Positioning, Countdown, Recording, Paused, Processing, Preview };
struct CaptureRegion { QScreen* screen; QRect rect; };
struct RecordingSettings { int fps; OutputFormat format; QualityPreset quality; bool showCursor; bool showClicks; bool countdown; QString outputDir; };
```

## Border Colors

- Idle/Positioning: `#94A3B8` (slate)
- Recording: `#EF4444` (red)
- Paused: `#FACC15` (yellow)

## macOS Permissions

Any macOS permission (screen recording, accessibility, microphone, etc.) **must** be requested at the very top of `AppController::start()` via a dedicated `request*Permission()` function in `platform/macos_window.h`. Never trigger permission prompts from inside a worker, manager constructor, or anything else — they must all fire early and together so the system dialogs appear at a predictable moment. The pattern:

```cpp
// AppController::start()
#ifdef Q_OS_MAC
    requestScreenRecordingPermission();
    requestAccessibilityPermission();
    // add future ones here
#endif
```

The `request*` functions live in `src/platform/macos_window.h` / `macos_window.mm`. Components that depend on a permission (e.g. `GlobalInputManager`) check `AXIsProcessTrusted()` / `CGPreflightScreenCaptureAccess()` at construction time and silently degrade if not yet granted — they do **not** re-prompt.
