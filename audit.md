# Architectural Audit — Framelit

_Current audit: 2026-07-04. Reflects the ship-readiness polish pass. See `CLAUDE.md` for architecture._

Status tags: **[OPEN]**, **[FIXED]**, **[DEFERRED]**.

---

## Fixed in the polish pass

| Item | File(s) | What changed |
|------|---------|--------------|
| Crop math duplicated | `capture/cropgeometry.hpp` (new) | Extracted `physicalCropRect()`; GIF encoder + streaming strategy now share it. Unit-tested (`CropGeometryTest`). |
| `outputDir` not validated | `appcontroller.hpp` | `load()` now falls back to Movies when the stored dir is missing/unwritable, not only when absent. Unit-tested. |
| Encoder hang / double cleanup | `bufferedstrategy.{hpp,cpp}` | Single `stopEncoderThread()` teardown (destructor + finished + failed + timeout); added a 2-min watchdog that fails the encode instead of hanging on "Processing…". |
| Unbounded frame RAM | `capture/framestore.{hpp,cpp}` | Byte budget (~1.5 GiB); excess frames dropped, `bufferLimitReached()` fires once. |
| Region draggable off-screen | `appcontroller.cpp` | `onRegionChanged()` snaps to the screen under the region and clamps via `CaptureRegion::clampedTo()`. |
| GIF delay precision | `encoding/gifencoder.cpp` | Delay computed in floating point before rounding (>50 fps no longer collapses). |
| `m_suppressSignal` bool | `ui/capturewindow.{hpp,cpp}` | Replaced with RAII `QSignalBlocker`. |
| Aspect-ratio math in 3 places | `ui/uigeometry.hpp` (new) | Extracted `heightForAspect()` (now consistently rounded); used by ControlBar grip, hotkey resize, snap-to-aspect. Unit-tested. |
| Misnamed local `kOutputSize` | `streamingstrategy.cpp` | Renamed `outputSize`. |
| Hardcoded code-sign identity | `CMakeLists.txt` | Now defaults to ad-hoc, overridable via `-D` / `FRAMELIT_CODESIGN_IDENTITY` env (CI already passes it). |
| Branding split | `main.cpp`, `appcontroller.cpp` | App/org set to Framelit / Kushview / kushview.net; explicit `QSettings("sc","ScreenCapture")` calls replaced with the app-wide identity. (Existing users reset — documented in README.) |
| Dead `AppState` values | `appcontroller.{hpp,cpp}` | Removed `Positioning`/`Countdown` + stale guard. |
| Dead `RecordingSettings` fields | `appcontroller.hpp` | Removed `showCursor`/`showClicks`/`countdown` + their QSettings keys; tests updated. |
| `macos_window.h` extension | renamed → `.hpp` | Updated 7 includes + CMake. |
| Dimension label (was "permanently hidden") | `ui/capturewindow.cpp` | Already resolved — `m_statusItem`/`m_dimsItem` are live. |
| MousePanner sentinel | `mousepanner.hpp` | Already documented (`<0 = unlimited`). |

---

## Still open

### 1. AppController is a God Class **[OPEN]**
`appcontroller.{hpp,cpp}` (~1000 lines) still owns UI windows, worker/thread lifecycle, strategy wiring, state transitions, settings, resize math, follow-mouse timer, and hotkeys. Recommended: extract a **`WorkerManager`** (owns `QThread` + `RecorderWorker` + `RecordingStrategy`, incl. the `m_frameConn` disconnect-before-`deleteLater`). Largest change; needs an interactive record→encode→preview verification pass. A dedicated `AppStateMachine` type stays deferred.

### 2. ControlBar mutates CaptureWindow directly **[OPEN — partially fixed]**
The programmatic round-trip is now `QSignalBlocker`-guarded (see Fixed), and demo-mode already routes through a signal. Remaining: `ControlBar::mouseMoveEvent` calls `m_captureWindow->setGeometry()` / `move()` directly (drag-resize logic in the view). Route through an intent signal to AppController. Needs interactive drag verification.

### 3. ControlBar inline stylesheets **[OPEN]**
~8 per-widget `setStyleSheet(...)` blocks. Moving them to `:/styles/controlbar.qss` needs objectName selectors and changes Qt's stylesheet cascade, so it wants a visual check on the running app. (The stale "250-line stylesheet" from prior audits no longer applies — it's ~40 lines.)

### 4. Backend split-construction unenforced **[OPEN]**
`capture/screencaptureworker.cpp` constructs the backend partly on the main thread and partly on the worker thread, enforced only by comments. Add `Q_ASSERT(QThread::currentThread() == …)` in both sections.

### 5. UI constants per-file **[DEFERRED]**
`kBarHeight`/`kBarMargin` (ControlBar file-statics), `kGripSize` (ControlBar class-static), `kBorderWidth`/`kMinDimension` (CaptureWindow class-statics). These are already reasonably scoped; a shared `constants.hpp` would couple unrelated widgets. Low/negative value — intentionally not done.

### 6. State-machine validation scattered **[DEFERRED]**
Per-slot `if (m_state != X) return;` guards. A `transition(from,to,fn)` helper is a nice-to-have; do it with the `WorkerManager` split.

### 7. Serial permission requests **[DEFERRED — deliberate]**
Accessibility is skipped when screen recording is denied. **Not a bug**: macOS shows one TCC dialog at a time and `CGRequestScreenCaptureAccess()` is async, so firing both hides the accessibility dialog. Documented in code + `CLAUDE.md`.

---

## What is good

- `Actions` registry + `SystemTray` — clean shared-`QAction` propagation.
- `MousePanner` — pure, stateless, well-tested.
- `RecorderWorker` / `RecordingStrategy` — clean interfaces, documented thread contract.
- `ScreenCaptureBackend` / `SckBackend` — solid platform abstraction.
- `FrameStore` — simple correct producer/consumer, now with thread assertions + a memory cap.
- `GifEncoder` (giflib) and `VideoEncoder` (Qt Multimedia CRF) — right-sized.
