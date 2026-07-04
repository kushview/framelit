# Framelit — Agent & Contributor Guide

Canonical guidance for working in this repo. This file is the single source of truth; `.github/copilot-instructions.md` points here.

## Project

Framelit is a **Pro screen-capture app** built in C++/Qt6 — an always-on-top region recorder with inline preview/edit and a minimal, utility-first UI. Product vision: **capture → render → record → sync**. Today capture, render, and record ship; **sync is not yet built** (see Roadmap).

The capture window is the product — it should feel like a fast utility, not an editor.

> The code lives in `namespace sc {}` — historical, from the project's original "Screen Capture" name. Keep it: the QSettings/bundle identity and moc symbols depend on stability, not on matching the product name.

## Build & Run

- **Configure + build:** `cmake -B build && cmake --build build`
- **Run:** `open build/Framelit.app` (macOS) · `./build/framelit` (Linux)
- **Tests:** `ctest --test-dir build --output-on-failure`
  - Suites: `CaptureRegionTest`, `RecordingSettingsTest`, `MousePannerTest`
- **Requirements:** Qt ≥ 6.5 (repo pins paths for 6.11.0 in `CMakeLists.txt`); C++20; CMake ≥ 3.22.
- **giflib:** found via Homebrew (`/opt/homebrew/opt/giflib`) or fetched/built via `FetchContent` fallback.
- **Linux deps:** `libgif-dev`, plus `Xext`/`X11` (real click-through via XShape).

## Principles

- **KISS** — always prefer the simplest solution that works. No clever abstractions, no over-engineering.
- **DRY** — if logic is written twice, it belongs in one place.
- Avoid adding features, refactors, or "improvements" beyond what was asked.

## Conventions

- All code in `namespace sc {}`. `main.cpp` is the only file outside it.
- Filenames: `alllowercase.hpp` / `alllowercase.cpp`. **No `.h`** — use `.hpp` for headers.
- Source layout: flat `src/`, plus subdirectories `src/ui/`, `src/capture/`, `src/encoding/`, `src/platform/`. New UI classes go in `src/ui/`; non-UI helpers stay flat in `src/`.

## Architecture

`AppController` (`src/appcontroller.{hpp,cpp}`) is the coordinator and owns the state machine (`AppState` enum), the UI surfaces, and the recording worker lifecycle. Workers run on `QThread`s and communicate back via signals.

### UI surfaces (each is a self-contained owner)
- **CaptureWindow** — transparent frameless overlay; drag/resize; click-through while recording.
- **ControlBar** — dark bar docked below the capture rect; polls `CaptureWindow::geometry()` via a 16 ms `QTimer` to stay snapped (no signals needed).
- **SystemTray** — owns the `QSystemTrayIcon` + `QMenu`; inserts shared `Actions`.
- **MainMenu** — owns the native `QMenuBar`; inserts shared `Actions`.
- **EditWindow** — preview/edit surface: left output-file list, center `QGraphicsView` compositor, transport controls below.
- **PreferencesDialog** — settings UI (extracted from ControlBar).
- **CenterHandle** / **CloseButton** — small overlay affordances.

### Capture → encode pipeline
```
ScreenCaptureBackend        SckBackend (macOS 12.3+, ScreenCaptureKit)
   (abstract)          ┐    QtScreenBackend (other platforms, QScreenCapture)
                       │
ScreenCaptureWorker  ← runs backend on a worker QThread; emits frameReady
   │
FrameStore           ← thread-safe append-only frame buffer
   │
RecordingStrategy    ┐    BufferedStrategy → GifEncoder   (GIF: buffer then encode)
   (abstract)        ┘    StreamingStrategy → VideoEncoder (MP4/WebM: stream live)
```
`AppController` picks the concrete strategy at record-start from `OutputFormat`; nothing else knows which is active.

### Preview behavior
- Preview opens explicitly via the `Open Preview` action. Stopping a recording does **not** auto-open it. If preview is already open when a new recording finishes, it selects the latest output.

## Qt GUI Class Design Rules

Apply to every UI component (`ControlBar`, `SystemTray`, `MainMenu`, `EditWindow`, `PreferencesDialog`, future panels):

- **One owner, one concern.** A class that owns a widget owns every action/menu/sub-widget inside it. Internal action pointers never leak into other classes.
- **Push, don't pull.** Controllers push state into UI via a single `sync(state, settings, …)` method. UI never reads from the controller.
- **Signals out, slots in.** UI emits coarse intent signals (`recordRequested`, `formatChangeRequested`); the controller maps them to handler slots. UI never calls controller methods — or platform functions, or another window's `setGeometry()` — directly.
- **`QSignalBlocker` for programmatic state.** Wrap `setChecked`/`setValue` during `sync()` in a `QSignalBlocker` to avoid re-entrancy. Prefer RAII blockers over bool guard flags.
- **No action/menu state in the controller.** More than one `QAction*` member on a controller is a smell — move them into a dedicated UI class.

## Shared Actions — `Actions`

`Actions` (`src/ui/actions.{hpp,cpp}`) is the central `QAction` registry. It owns every `QAction` and exposes them as plain public pointers (`actions->record`, `actions->followMouse`).

**Key property:** `setEnabled`/`setChecked`/`setText` on one shared `QAction*` updates *every* menu and toolbar holding it — no per-surface sync loop. Pattern:
1. `AppController` creates `Actions`, connects its signals to handler slots, calls `m_actions->sync(state, settings, …)` after any state change.
2. UI surfaces receive `Actions*` and insert shared pointers (`menu->addAction(actions->record)`).
3. `Actions::sync()` updates state; Qt propagates to all surfaces automatically.

This is the idiomatic Qt action-sharing model (same one Qt Creator uses internally).

## Compositing

`QGraphicsScene` is the designated compositor. All visual layers — screen frame, camera PIP, annotations — should be `QGraphicsItem` subclasses in a shared scene; Qt handles z-order, transforms, opacity, dirty tracking. Use `QGraphicsScene::render()` for CPU output (GIF). For future GPU video output, prefer `QRhi` (Metal on macOS). **Never roll a custom layer/compositor** when `QGraphicsScene` already provides it.

## Threading contracts

- **FrameStore** — append-only producer/consumer. During recording the producer (capture worker) thread calls `addFrame()`; the main thread must not call `frameAt()`. `Q_ASSERT` guards enforce the thread contract at runtime.
- **RecordingStrategy** — `onFrame()` and `finish()` are called on the **main** thread (`Qt::QueuedConnection` from the worker). Subclasses may spawn their own encoder threads internally. Disconnect the `frameReady → onFrame` connection before `deleteLater()`-ing a strategy so queued frames don't execute against a deleted object.
- **ScreenCaptureBackend** — created on the main thread, moved to the worker thread; `startCapture()`/`stopCapture()` run on the worker. `frameArrived()` may fire from any thread (use queued connections). SCK backend is split-constructed (main-thread NSView access for window exclusion, then worker-thread init).

## macOS Permissions

Any macOS permission (screen recording, accessibility, microphone, …) **must** be requested at the very top of `AppController::start()` via a dedicated `request*Permission()` function in `src/platform/macos_window.h`. Never trigger a permission prompt from a worker, a manager constructor, or anywhere else — they fire early so system dialogs appear at a predictable moment.

They are requested **serially, not all at once**: macOS (TCC) shows only one permission dialog at a time, and `CGRequestScreenCaptureAccess()` returns *before* its dialog is dismissed. Firing accessibility immediately after would open System Settings silently behind the screen-recording dialog, where the user never sees it. So screen recording is requested first; accessibility follows only once screen recording is already granted (otherwise it's requested on the next launch). Preserve this ordering when adding new permissions.

```cpp
// AppController::start()
#ifdef Q_OS_MAC
    if (requestScreenRecordingPermission()) // async; returns before dialog closes
        requestAccessibilityPermission();   // only if SR already granted
    requestMicrophonePermission();
#endif
```

Components that depend on a permission (e.g. `GlobalInputManager`) check `AXIsProcessTrusted()` / `CGPreflightScreenCaptureAccess()` at construction and silently degrade if not yet granted — they do **not** re-prompt.

## Key Types

```cpp
enum class AppState { Idle, Recording, Paused, Processing, Preview };
enum class OutputFormat { Gif, Mp4, WebM };
enum class QualityPreset { Low, Medium, High };

struct CaptureRegion {
    QScreen* screen = nullptr;
    QRect    rect;
    // clampedTo(screenBounds): returns a region fully inside bounds.
};

struct RecordingSettings {
    int fps;
    OutputFormat  format;
    QualityPreset quality, gifQuality;
    bool captureAudio;   // mic audio muxed into MP4; no effect on GIF
    bool hiDpi;          // 2× output resolution
    bool letterbox;      // preserve aspect (vs stretch to fill)
    bool demoMode;       // when true, app windows are visible to external recorders
    QSize outputSize, gifOutputSize;
    bool gifUseFrameSize;
    QString audioDeviceId, audioOutputDeviceId, outputDir;
    int growStep;        // px per grow/shrink hotkey press
    // load(QSettings&) / save(QSettings&)
};
```

## Border Colors

- Idle: `#94A3B8` (slate)
- Recording: `#EF4444` (red)
- Paused: `#FACC15` (yellow)

## Global Hotkeys (macOS)

Requires Accessibility permission (System Settings → Privacy & Security → Accessibility).

| Shortcut | Action |
|---|---|
| `Cmd+Shift+=` | Grow capture region |
| `Cmd+Shift+-` | Shrink capture region |
| `Cmd+Shift+F` | Toggle follow-mouse |

## Roadmap

- ✅ **Capture / Render / Record** — region capture, GIF/MP4/WebM output, inline preview/edit, global hotkeys.
- 🔜 **Sync** — cloud upload / shareable links. **Not yet designed or implemented**; no upload/cloud code exists in `src/`. When built, it belongs behind a service abstraction wired into `AppController`, invoked after `encodingFinished` — not inside encoders or workers.
