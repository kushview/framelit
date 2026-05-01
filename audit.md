# Architectural Audit — screen-capture-qt

_Audited: 2026-05-01 · Re-audited: 2026-05-01 (outputsize branch)_

Changes since last audit are marked **[NEW]** or **[UPDATED]**.

---

## Critical

### 1. AppController is a God Class
**Files:** `src/appcontroller.hpp`, `src/appcontroller.cpp`

AppController (~520 lines) owns: both UI windows, worker thread lifecycle, strategy selection and wiring, state machine transitions, settings persistence, aspect-ratio resize logic, follow-mouse pan timer, and the macOS hotkey manager. Any single bug touches half the file. Recommended split:

- `AppStateMachine` — pure state transition table; guards and fires on transition
- `WorkerManager` — owns QThread, RecorderWorker, RecordingStrategy lifecycle
- `AppController` — thin coordinator: wires the above, owns windows

---

### 2. Bidirectional `regionChanged` Coupling
**Files:** `src/appcontroller.cpp`, `src/ui/controlbar.cpp`

Two independent code paths modify CaptureWindow geometry:
1. CaptureWindow emits `regionChanged` → AppController → re-emits `regionChanged` → CaptureWindow applies it (round-trip, guarded by `m_suppressSignal`)
2. `ControlBar::mouseMoveEvent` calls `m_captureWindow->setGeometry()` directly, bypassing AppController entirely

The `m_suppressSignal` bool flag is the only thing preventing an infinite loop. An RAII signal blocker (`QSignalBlocker`) would be safer and more idiomatic.

---

### 3. ControlBar Does Too Much
**Files:** `src/ui/controlbar.cpp` (~400 lines)

- `mouseMoveEvent` directly resizes CaptureWindow and implements grip-zone hit-testing (business logic in the view)
- Preferences dialog built and managed inline — should be a `PrefsDialog` class
- Demo mode toggle directly calls platform functions (`setWindowCaptureExcluded`) instead of signalling AppController
- Snap-to-region geometry calculation duplicated from AppController
- Audio device list populated at construction (no caching; refills on every instantiation)
- 250-line inline stylesheet string — unmaintainable; belongs in a `.qss` resource file

---

### 4. Strategy Use-After-Free Risk
**Files:** `src/appcontroller.cpp`

Frames are enqueued for `m_strategy->onFrame()` via `Qt::QueuedConnection` from the worker thread. When `onRecordingFinished()` fires, `m_strategy->deleteLater()` is called — but frames already in the event queue execute against the strategy *after* it has been marked for deletion. The fix: disconnect the `frameReady` → `onFrame` connection before issuing `deleteLater()`.

---

### 5. FrameStore TOCTOU — Contract Unenforced
**Files:** `src/capture/framestore.hpp`, `src/capture/framestore.cpp`

Comment states: "During recording the main thread must not call `frameAt()` — only `addFrame()` and `clear()` are safe." No assertions, no thread-ID checks, no mutex between `clear()` and `frameAt()`. The contract is documentation-only and will silently corrupt if violated. Add `Q_ASSERT(QThread::currentThread() == …)` guards at minimum.

---

### 6. BufferedStrategy Destructor Blocks + Double Cleanup
**Files:** `src/bufferedstrategy.cpp`

`~BufferedStrategy()` calls `m_encoderThread->quit()` + `wait()` synchronously. Blocking in a destructor is problematic if the destructor is called from a non-main-thread context or during Qt teardown. The encoder thread is also quit/waited a second time in the `encodingFinished`/`encodingFailed` handlers — double cleanup that can race.

---

## Moderate

### 7. QGraphicsScene Not Used as Compositor **[UPDATED]**
**Files:** `src/ui/capturewindow.cpp`

The architecture doc specifies QGraphicsScene as the shared compositor for screen frame, camera PIP, and annotations. In practice the scene holds only the border rect, dimension label (currently hidden — see #15), and resize handles. GIF and video encoding bypass the scene and process raw QImages directly. This is acceptable for the current feature set but blocks future camera PIP or annotation layers. Track it as a deliberate deferral rather than a todo comment.

### 8. Unused AppState Values
**Files:** `src/appcontroller.hpp`

`AppState::Countdown` and `AppState::Positioning` are defined but no state transition leads to them. `AppState::Preview` is also defined but no code enters it. Either remove them or implement them — dead enum values cause confusion when reading state guards.

### 9. Aspect Ratio Lock Logic in Three Places
**Files:** `src/ui/capturewindow.cpp`, `src/ui/controlbar.cpp`, `src/appcontroller.cpp`

The constraint calculation (derive new W/H from a delta while holding ratio) appears three times with slight variations. Extract a free function `QSize constrainToAspect(QSize current, QSizeF ratio)` in a shared header.

### 10. Frame Crop Rect Duplicated
**Files:** `src/encoding/gifencoder.cpp`, `src/streamingstrategy.cpp` **[NEW]**

Both files independently compute a crop rect from a full-screen QImage to the capture region, including HiDPI scaling. Any precision or rounding difference is a latent bug. Extract `QRect physicalCropRect(const CaptureRegion&, QSize frameSize)` as a free function in `capture/`.

### 11. State Machine Validation Scattered
**Files:** `src/appcontroller.cpp`

Each slot handler independently guards with `if (m_state != AppState::X) return;`. No central transition table; an invalid transition added in the future will silently no-op. A `transition(AppState from, AppState to, std::function<void()> action)` helper would make invalid transitions assertions.

### 12. Settings Not Validated on Load
**Files:** `src/appcontroller.hpp` (`RecordingSettings::load`)

`outputDir` is restored from QSettings without checking whether the directory exists or is writable. Recording fails with a cryptic encoder error if the path has been deleted or moved. Fall back to `QStandardPaths::MoviesLocation` if invalid.

### 13. RecordingSettings Has Dead Fields **[NEW]**
**Files:** `src/appcontroller.hpp`

`showCursor`, `showClicks`, and `countdown` are declared in `RecordingSettings` and persisted to QSettings but are never read by any worker or encoder. Remove or implement them.

### 14. Backend Split-Construction is Fragile **[UPDATED]**
**Files:** `src/capture/screencaptureworker.cpp`

The backend is partially constructed on the main thread (for `setExcludedWindowIds` NSView access) and partially initialised on the worker thread. This contract is documented in a comment but has no compile-time or runtime enforcement. A future backend author will not see the comment and will get intermittent crashes. Add an `Q_ASSERT(QThread::currentThread() == ...)` in both the main-thread and worker-thread sections.

---

## Minor

| # | File(s) | Issue |
|---|---------|-------|
| 15 | `src/ui/capturewindow.cpp` | `m_labelItem->setVisible(false)` — dimension label is fully implemented but permanently hidden. Remove or re-enable. |
| 16 | Multiple | `m_suppressSignal` bool — replace with `QSignalBlocker` RAII guard |
| 17 | Multiple | Magic numbers (`kBarHeight`, `kGripSize`, `kBorderWidth`, `kHandleSize`, `kMinDimension`) defined per-file — move to `src/ui/constants.hpp` |
| 18 | `src/appcontroller.cpp` | If screen recording permission is denied on first launch, accessibility permission is never requested until next launch (sequential early-exit) |
| 19 | `src/appcontroller.cpp` | `onRegionChanged()` does not clamp rect to screen bounds; dragging off-screen causes silent capture failure |
| 20 | `src/ui/capturewindow.hpp` | `lockedAspect()` is public but only consumed by ControlBar — encapsulation leak |
| 21 | `src/platform/qtscreenbackend.cpp` | Cursor compositing is `#if 0`'d — remove dead code or replace with a TODO comment |
| 22 | `src/platform/sckbackend.mm` | Same cursor dead code (`#if 0`) — same recommendation |
| 23 | `src/encoding/gifencoder.cpp` | `qMax(1, 100 / outputFps)` delay calculation loses precision above 50 fps — use floating-point intermediate |
| 24 | `src/capture/framestore.hpp` | No frame buffer size limit; 30s @ 30fps ≈ 1.2 GB RAM. Document a hard cap or add a configurable one |
| 25 | `src/bufferedstrategy.cpp` | No timeout if encoder hangs; UI will show "Processing…" forever |
| 26 | `src/ui/controlbar.cpp` | 250-line inline stylesheet string — move to `:/styles/controlbar.qss` resource |
| 27 | `src/streamingstrategy.cpp` | Local variable named `kOutputSize` uses `k` const-naming convention but is a mutable local copy — rename to `outputSize` |
| 28 | `CMakeLists.txt` | Code-signing identity hardcoded (`ED7BA643...`) — should be an env variable or cmake option |
| 29 | `src/mousepanner.hpp` | Default `outerThreshold = -1` (unlimited) is surprising for a new caller; comment this clearly on the struct |

---

## What Is Actually Good

- `MousePanner` — pure stateless utility, zero coupling, well-tested, clean speed-ramp math
- `RecorderWorker` / `RecordingStrategy` — clean abstract interfaces; thread contract documented
- `ScreenCaptureBackend` — clean platform abstraction; SCK backend correctly handles buffer lifecycle and thread dispatch
- `FrameStore` — simple and correct; append-only producer/consumer design is right
- `GifEncoder` — solid giflib integration; palette quantization, dithering, loop extension all correct
- `VideoEncoder` — thin Qt Multimedia wrapper; CRF quality mode is the right call
- `SckBackend` — native SCK integration is sophisticated; window exclusion, dispatch queue bridging, and pixel buffer handling are all done correctly
- State-machine approach in `AppController` is the right pattern — just needs to be extracted into its own type

---

## Priority Order

| Priority | Item | Risk if deferred |
|----------|------|-----------------|
| 1 | Fix strategy race (#4) — disconnect before `deleteLater` | Active crash |
| 2 | `FrameStore` assertions (#5) | Silent corruption |
| 3 | Extract `physicalCropRect` (#10) | Latent precision bug between GIF and MP4 output |
| 4 | Validate `outputDir` on load (#12) | Cryptic failure on first record after dir deleted |
| 5 | Remove/implement dead `RecordingSettings` fields (#13) | Confusion, wasted QSettings keys |
| 6 | Replace `m_suppressSignal` with `QSignalBlocker` (#16) | Fragile, easy to break on refactor |
| 7 | Centralise aspect-ratio logic (#9) | Bug fix in one place won't propagate |
| 8 | Split `AppController` (#1) | Growing complexity tax |
| 9 | Refactor `ControlBar` (#3) | Growing complexity tax |
| 10 | Centralise UI constants (#17) | Low risk, high readability payoff |


