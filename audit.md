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
| AppController God Class | `workermanager.{hpp,cpp}` (new) | Extracted `WorkerManager` — owns the worker QThread, strategy, frame routing, and the disconnect-before-`deleteLater` dance. AppController keeps only state/UI policy and maps lifecycle signals. |
| ControlBar mutated CaptureWindow | `ui/controlbar.{hpp,cpp}`, `appcontroller.cpp` | Drag/resize now emits `captureRectChangeRequested`; AppController applies it (clamped) via `onRegionChanged`. No more direct `setGeometry()`/`move()` from the view. |
| ControlBar inline CSS | `resources/controlbar.qss` (new) | ~8 per-widget stylesheets moved into a scoped resource with objectName selectors. |
| Backend split-construction unenforced | `capture/screencaptureworker.cpp` | Added `Q_ASSERT` main-thread (construction) and worker-thread (start/stop) guards. |
| GIF "blue whites" (pre-existing) | `encoding/gifencoder.cpp` | Qt's fixed Indexed8 cube puts pure white at index 215; the palette-budget step kept the *first* N table entries and remapped white → cyan. Now keeps the N **most-used** colors by histogram (white dominates screen content, so it survives) and threshold-maps flats. Verified white is retained at a 128-color budget. |

---

## Still open / deferred

### 1. UI constants per-file **[DEFERRED]**
`kBarHeight`/`kBarMargin` (ControlBar file-statics), `kGripSize` (ControlBar class-static), `kBorderWidth`/`kMinDimension` (CaptureWindow class-statics). These are already reasonably scoped; a shared `constants.hpp` would couple unrelated widgets. Low/negative value — intentionally not done.

### 2. State-machine validation scattered **[DEFERRED]**
Per-slot `if (m_state != X) return;` guards. A `transition(from,to,fn)` helper is a nice-to-have; not urgent now that the `WorkerManager` split has landed.

### 3. `lockedAspect()` encapsulation leak **[DEFERRED]**
`CaptureWindow::lockedAspect()` is public and read by `ControlBar` during grip resize. Minor; would fold naturally into a future state/aspect owner.

### 4. Serial permission requests **[DEFERRED — deliberate]**
Accessibility is skipped when screen recording is denied. **Not a bug**: macOS shows one TCC dialog at a time and `CGRequestScreenCaptureAccess()` is async, so firing both hides the accessibility dialog. Documented in code + `CLAUDE.md`.

---

## What is good

- `Actions` registry + `SystemTray` — clean shared-`QAction` propagation.
- `MousePanner` — pure, stateless, well-tested.
- `RecorderWorker` / `RecordingStrategy` — clean interfaces, documented thread contract.
- `ScreenCaptureBackend` / `SckBackend` — solid platform abstraction.
- `FrameStore` — simple correct producer/consumer, now with thread assertions + a memory cap.
- `GifEncoder` (giflib) and `VideoEncoder` (Qt Multimedia CRF) — right-sized.
