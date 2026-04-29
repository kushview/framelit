# Qt6 Screen Capture App Plan

## Working Concept

Build a lightweight screen capture app inspired by Cockos LICEcap, but modernized with a faster workflow, better output quality, inline preview/editing, and optional sharing.

The core product idea is:

> A tiny, always-on-top capture frame that lets users record a screen region, immediately preview/trim/copy/share the result, and stay out of the way.

The app should feel closer to a utility than a full editor. The capture window is the product.

---

# Product Goals

## Primary User Promise

Record a region of the screen and share it in seconds.

## Core Differentiators vs LICEcap

- Modern minimal UI
- Movable/resizable capture frame
- Click-through capture region
- Inline preview after recording
- Instant trim
- Clipboard-first workflow
- Better GIF optimization
- Optional MP4/WebM export
- Optional upload/share links later

## Target Users

Initial target users:

- Developers recording bugs or UI behavior
- QA testers capturing repro steps
- Support teams sending visual explanations
- Designers showing micro-interactions
- Indie makers who want fast GIF/MP4 snippets

---

# High-Level Architecture

The app should be split into these layers:

```text
Qt UI layer
  ├─ capture frame window (QGraphicsView + transparent QGraphicsScene, frameless)
  ├─ control bar window (QWidget, frameless)
  ├─ preview/editor UI (QGraphicsView + QOpenGLWidget viewport)
  └─ settings popover (QMenu + custom QFrame)

GPU scene graph (QGraphicsScene per window)
  ├─ capture overlay scene: QGraphicsRectItem border + dimension label item
  ├─ preview scene: QGraphicsVideoItem for frame display (GPU-composited)
  ├─ annotation layer: QGraphicsItem subclasses (live, interactive, no CPU burn-in)
  └─ effects layer: QGraphicsBlurEffect on blur/redact annotation items

recording service
  ├─ screen capture backend (CaptureService: QScreenCapture → QMediaCaptureSession → QVideoSink)
  ├─ frame buffering (FrameStore: thread-safe QVideoFrame buffer)
  ├─ cursor/click overlay capture
  ├─ frame timing/FPS controller (QTimer + QElapsedTimer)
  └─ recording state machine (manual AppState enum in AppController)

encoding service
  ├─ GIF encoder (GifEncoder: frame decimation, palette quantization, scaling, giflib)
  ├─ MP4/WebM encoder (Mp4Encoder: QVideoFrameInput + QMediaRecorder)
  ├─ shared interface (EncoderBase: encodingProgress, encodingFinished, encodingFailed signals)
  ├─ annotation export (ExportRenderer: QPainter burn of AnnotationStore onto QImage)
  └─ output optimization

storage/share service
  ├─ local file save (QFile, QStandardPaths)
  ├─ clipboard copy (QClipboard)
  ├─ recent captures (QSettings)
  └─ future cloud upload
```

Key Qt6 modules and classes:

| Purpose | Qt class / module |
|---|---|
| UI windows | `QWidget`, `QFrame`, `QDialog` |
| Scene graph | `QGraphicsScene`, `QGraphicsView` |
| GPU viewport | `QOpenGLWidget` (as `QGraphicsView` viewport), `QRhiWidget` (Qt 6.6+) |
| Video display (GPU) | `QGraphicsVideoItem` (`Qt6::MultimediaWidgets`) |
| Capture border overlay | `QGraphicsRectItem` inside a transparent `QGraphicsScene` |
| Annotation items (live) | `QGraphicsItem` subclasses — `ArrowItem`, `RectItem`, `BlurItem`, `TextItem` |
| Annotation effects | `QGraphicsBlurEffect`, `QGraphicsEffect` |
| Screen capture | `QScreenCapture` (Qt 6.5+), `QScreen::grabWindow()` |
| GPU frame pipeline | `QVideoSink`, `QVideoFrame`, `QMediaCaptureSession` |
| Frame timing | `QTimer`, `QElapsedTimer` |
| GIF encoding | `giflib` via CMake `FetchContent`; `GifExportSettings` controls output FPS, max width, palette |
| MP4/WebM | `QMediaRecorder` or external `ffmpeg` subprocess |
| Clipboard | `QClipboard` (via `QGuiApplication::clipboard()`) |
| Global hotkeys | `QHotkey` (third-party) or `QShortcut` |
| Settings persistence | `QSettings` |
| JSON config | `QJsonDocument`, `QJsonObject` |
| File paths | `QStandardPaths`, `QDir` |

---

# Window Model

Use multiple windows instead of one monolithic iced window.

## 1. Capture Region Window

Purpose:

- Shows the capture boundary
- Defines the screen rectangle being recorded
- Stays always on top
- Usually click-through so the user can interact with the app being recorded

Behavior:

- Transparent background
- Visible border
- Optional subtle shadow/glow
- Red border while recording
- Yellow border while paused
- Displays live dimensions while resizing

Implementation approach:

Use a frameless `QGraphicsView` with a transparent `QGraphicsScene`. The visible border is a `QGraphicsRectItem` with a colored `QPen` (no fill). A `QGraphicsSimpleTextItem` shows live dimensions. This lets the border and label be scene items composited by the GPU rather than repainting the entire widget on every state change.

```cpp
// GPU-composited capture overlay
auto* scene = new QGraphicsScene(this);
auto* view  = new QGraphicsView(scene, this);
view->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
view->setAttribute(Qt::WA_TranslucentBackground, true);
view->setStyleSheet("background: transparent;");
view->setFrameShape(QFrame::NoFrame);

// The border item — state changes just call setPen(), no full repaint
m_borderItem = scene->addRect(scene->sceneRect(), QPen(Qt::red, 2));
m_labelItem  = scene->addSimpleText("800×450");
```

Click-through during recording:

```cpp
// Set on the QGraphicsView (the actual window widget)
view->setAttribute(Qt::WA_TransparentForMouseEvents, true);
```

Platform notes:

- Windows: `WA_TransparentForMouseEvents` works reliably; also `SetWindowLong` with `WS_EX_TRANSPARENT` for belt-and-suspenders
- macOS: `WA_TransparentForMouseEvents` works; may need `NSWindow.ignoresMouseEvents` for full passthrough
- Linux/X11: works via `_NET_WM_STATE` and shape extension; Wayland support varies

## 2. Control Bar Window

Purpose:

- Contains interactive controls
- Not click-through
- Attached visually to the capture frame

Placement:

- Usually docked under the capture rectangle
- Can flip above if near bottom of screen
- Always on top

Controls:

```text
● REC | 30 FPS | 800×600 | Start | Settings | Folder | Close
```

During recording:

```text
⏺ Recording... | Pause | Stop | 00:12
```

## 3. Preview/Edit Window or Mode

After recording stops, the app should transition into preview mode using the same general window footprint.

Preview controls:

```text
Play/Pause | Timeline | Trim | Annotate | Save | Copy | Share | Close
```

---

# UX State Machine

Keep the app state simple and linear.

```text
Idle
  ↓
Positioning
  ↓ Start
Countdown
  ↓
Recording
  ↓ Pause/Resume
Paused
  ↓ Stop
Processing
  ↓
Preview
  ↓ Save/Copy/Share/Close
Idle
```

## State Descriptions

### Idle

The app is open but not recording.

User can:

- Move capture frame
- Resize capture frame
- Change FPS
- Change output format
- Start recording
- Open settings

### Positioning

The user is actively moving/resizing the frame.

UX details:

- Show live dimensions
- Snap optionally to common sizes
- Keep controls visible but unobtrusive

### Countdown

Optional countdown before capture begins.

Default:

- 3 seconds
- Can be disabled in settings

### Recording

The app captures frames from the selected rectangle.

UX details:

- Border turns red
- Timer starts
- Control bar switches to recording controls
- Capture frame should remain movable if feasible
- Capture region should allow click-through

### Paused

Recording temporarily pauses.

UX details:

- Border turns yellow
- Timer pauses
- User can resume or stop

### Processing

Frames are being encoded or optimized.

UX details:

- Show lightweight progress
- Avoid blocking the UI thread

### Preview

The recording is shown in a loop.

User can:

- Play/pause
- Scrub
- Trim start/end
- Annotate
- Save
- Copy to clipboard
- Upload/share later

---

# Phase 1 — Better Than LICEcap

Goal:

Create a fast, polished local recorder that users can trust.

This phase is about adoption, not monetization.

## Phase 1 Product Features

### Capture Frame

- Movable capture region
- Resizable edges/corners
- Always-on-top
- Transparent center
- Visible border
- Live dimension display
- Control bar attached to frame

### Recording

- Start recording
- Stop recording
- Configurable FPS: 10, 15, 24, 30, 60
- Optional countdown
- Capture cursor if supported
- Basic click ripple overlay if practical

### Output

- GIF export first
- Optional PNG sequence for debugging
- MP4 can wait unless easy
- Save to user-selected or last-used folder
- Automatic file naming

Suggested default filename format:

```text
capture-YYYY-MM-DD-HHMMSS.gif
```

Later, allow smarter names:

```text
bug-login-error-2026-04-26.gif
```

### Clipboard

- Copy finished GIF to clipboard if platform supports it
- Otherwise copy file path
- Optionally reveal file in folder

### Settings

Settings should be a popover, not a full page.

Settings v1:

```text
Output:
  GIF   MP4

Recording FPS:
  10 / 15 / 24 / 30 / 60   ← how fast frames are captured

GIF Output FPS:
  5 / 10 / 15               ← independent; lower = smaller file

GIF Max Width:
  480 / 640 / 800 / Original

Quality:
  Low / Medium / High       ← palette size and dither level

Capture:
  [x] Show cursor
  [x] Show click effects
  [ ] Countdown before recording
```

Recording FPS and GIF output FPS are intentionally separate controls. A 30 FPS recording downsampled to 10 FPS for GIF export produces a smaller, smoother-looking result than recording at 10 FPS. GIF frame timing has a minimum resolution of 10ms (centisecond precision), so anything above 50 FPS is wasted.

## Phase 1 Technical Milestones

### Milestone 1: Basic Qt6 App Shell

Tasks:

- Create C++ project with CMake + Qt6 (`Core`, `Gui`, `Widgets`, `Multimedia`, `MultimediaWidgets`, `OpenGL`, `OpenGLWidgets`)
- Create a frameless always-on-top `QGraphicsView` for the capture region overlay; set a `QGraphicsScene` with a `QGraphicsRectItem` border and dimension label item
- Create control bar `QWidget` docked below it
- Define `AppState` enum and connect to UI state
- Set `Qt::WindowStaysOnTopHint` on both windows

### Milestone 2: Capture Rectangle Model

Tasks:

- Define `CaptureRegion { QScreen* screen; QRect rect; }`
- Show dimensions in control bar label
- Persist last region with `QSettings`
- Implement mouse drag + resize via `QRubberBand` or custom `mouseMoveEvent`

### Milestone 3: Screen Capture Prototype

Tasks:

- Use `QScreenCapture` (Qt 6.5+) or `QScreen::grabWindow(0)` fallback
- Crop captured `QImage` to `CaptureRegion` rect
- Save cropped PNG with `QImage::save()`
- Surface macOS screen recording permission errors clearly

### Milestone 4: Timed Recording Loop

Tasks:

- **Change `RecorderWorker::frameReady` from `QImage` to `QVideoFrame`** — this is the first task of this milestone. `QVideoFrame` is ref-counted so passing it between threads is a pointer bump with no copy. `toImage()` must only be called by the encoder, not by `ScreenCaptureWorker`. The Milestone 3 prototype called `toImage()` eagerly to save a test PNG; that stops here.
- Implement `FrameStore`: a thread-safe `QVector<QVideoFrame>` with a `QMutex`. `AppController` connects `frameReady(QVideoFrame)` to `FrameStore::addFrame()`. The encoder reads from `FrameStore` at encode time — never on the UI thread.
- FPS throttle is already implemented in `ScreenCaptureWorker` via `QElapsedTimer`; verify it holds up under load at 30 FPS
- Emit `progressUpdated(qint64 elapsedMs)` once per second (already implemented); connect to control bar timer label
- Update `AppController::onStartRequested` to remove the Milestone 3 prototype `onFirstFrameReceived` connection and connect `frameReady` to `FrameStore` instead

### Milestone 5: GIF Encoding

Tasks:

- Implement `GifEncoder` reading from `FrameStore` with `GifExportSettings`
- Frame decimation: take every `captureFps / outputFps`-th frame — e.g., record at 30, export at 10 → every 3rd frame
- Scale to `maxWidth` using `QImage::scaled()` with `Qt::SmoothTransformation` if needed
- Encode via `giflib` (CMake `FetchContent`); per-frame delay = `100 / outputFps` centiseconds
- Apply palette quantization and dithering based on `QualityPreset`
- Emit `encodingProgress(float)` and `encodingFinished(QString path, qint64 bytes)`
- Save to `outputDir` via `QStandardPaths::writableLocation`
- Surface the output file size in the control bar so the user can re-export at lower quality without re-recording

### Milestone 5.5: RecordingStrategy Abstraction

Introduce the `RecordingStrategy` interface now, while GIF is the only format. This is a pure refactor — no behavior change — but it flushes out any threading or lifetime issues early and prevents format-specific logic from accreting inside `AppController`.

Tasks:

- Define `RecordingStrategy` as a `QObject` base class in `src/recordingstrategy.hpp` with:
  - Pure virtual `void onFrame(const QImage& img, const CaptureRegion& region)`
  - Pure virtual `void finish()`
  - Signals: `encodingProgress(float)`, `encodingFinished(QString path)`, `encodingFailed(QString reason)`
- Implement `BufferedStrategy` in `src/bufferedstrategy.hpp/.cpp`: wraps `FrameStore` + `GifEncoder` exactly as `AppController` does today
- `AppController::onStartRequested()` constructs `BufferedStrategy` (only format for now), connects `ScreenCaptureWorker::frameReady` → `strategy->onFrame()`, and connects strategy signals back to controller
- `AppController::onRecordingFinished()` calls `strategy->finish()` instead of building the encoder directly
- Remove all encoder-construction code from `AppController` — it belongs in the strategy
- All GIF tests must still pass; behavior is identical

### Milestone 6: Preview Mode

Tasks:

- After encoding, transition to Preview `AppState`
- Create a `QGraphicsScene` backed by a `QGraphicsView`; set a `QOpenGLWidget` as the viewport (`view->setViewport(new QOpenGLWidget)`) so compositing runs through Qt's `QRhi` layer (Metal/D3D/GL depending on platform) — no raw OpenGL code required
- Add a `QGraphicsVideoItem` connected to a `QMediaPlayer` pointed at the encoded output file — GPU renders the video with zero CPU copies
- Drive a looping `QMediaPlayer` for GIF/MP4 preview; fall back to `QGraphicsPixmapItem` driven by a `QTimer` for frame-by-frame GIF playback if `QMediaPlayer` cannot loop GIF natively
- Add Save / Copy / Close buttons in the control bar
- Copy with `QGuiApplication::clipboard()->setImage()` using a frame extracted via `QVideoSink`

### Milestone 7: Polish Pass

Tasks:

- Improve border colors per state (idle/recording/paused)
- Add countdown using `QTimer` + overlay label
- Add global hotkey via `QHotkey` or `QKeySequence`
- Persist settings with `QSettings`
- Handle permission/capture errors with `QMessageBox`

## Phase 1 Success Criteria

A user can:

1. Launch app
2. Move/resize capture frame
3. Press Record
4. Interact with the app underneath the capture region
5. Stop recording
6. Preview output
7. Save or copy the GIF

---

# Phase 2 — Make It Indispensable

Goal:

Add workflow features that free tools often handle poorly.

This phase is where light monetization can begin.

## Phase 2 Product Features

### Inline Trim

After recording, user can trim start/end directly in preview mode.

UX:

```text
[ left trim handle ] ==== timeline ==== [ right trim handle ]
```

Requirements:

- Timeline scrubber
- Start/end handles
- Loop only selected region
- Re-encode trimmed result

### Annotations

Keep annotations lightweight.

Tools:

- Arrow
- Rectangle
- Highlight
- Blur/redact
- Text label
- Undo/redo

Important:

This should not become a full image editor.

### Better Clipboard Workflow

After recording:

- Auto-copy output if enabled
- Show a small confirmation: `Copied GIF to clipboard`
- Allow drag-out from preview

### Better Output Control

Add:

- MP4 export
- WebM export if useful
- GIF optimization presets
- Max width scaling
- Loop count option

### Workflow Quality Features

- Remember last capture size and position
- Recent captures list
- Open last capture
- Re-copy last capture
- Detect static frames to reduce GIF size
- Pause/resume recording

## Phase 2 Technical Milestones

### Milestone 1: Frame Timeline Model

`FrameStore` (service layer) holds the buffered `QVideoFrame` objects written by `CaptureService`. Define alongside it a `RecordingTimeline` that carries playback metadata:

```cpp
struct RecordingTimeline {
    int frameCount;          // total frames captured
    int fps;
    std::chrono::milliseconds trimStart;
    std::chrono::milliseconds trimEnd;

    // FrameStore provides the actual frames; toImage() is called only
    // at encode time, off the main thread, once per frame.
};
```

`PreviewController` reads `FrameStore::frameAt(i)` and calls `livePreviewSink->setVideoFrame(frame)` to drive the `QGraphicsVideoItem` — the GPU handles upload. `QImage` conversion happens only inside `GifEncoder` / `Mp4Encoder`, never in the controller or view.

### Milestone 2: Preview Playback Engine

Tasks:

- Create a `QGraphicsScene` for the preview window; set a `QOpenGLWidget` as the `QGraphicsView` viewport so all compositing happens on the GPU
- Add a `QGraphicsVideoItem` as the primary frame display item; connect it to a custom `QVideoSink` that the playback engine pushes frames into via `QVideoSink::setVideoFrame()`
- Drive frame advancement with a `QTimer` mapped to the `RecordingTimeline` frame index; each tick calls `sink->setVideoFrame(frames[i])` — the GPU handles the upload and render
- Add a `QGraphicsRectItem` trim-range overlay and `QGraphicsLineItem` scrubber marker as scene items on top of the video item
- Scrub via `QSlider`; slider value maps to frame index and calls `sink->setVideoFrame()` directly
- Loop playback by wrapping the frame index at `trimEnd`

### Milestone 3: Trim + Re-encode

Tasks:

- Export selected frame range
- Re-encode without re-recording
- Preserve quality settings

### Milestone 4: Annotation Data Model

Example:

```cpp
enum class AnnotationKind {
    Arrow,
    Rectangle,
    Highlight,
    Blur,
    Text
};

struct Annotation {
    QUuid id;
    AnnotationKind kind;
    QString text; // only used for Text kind
    std::chrono::milliseconds startTime;
    std::chrono::milliseconds endTime;
    QRectF bounds;
    QColor color;
    int strokeWidth;
};
```

### Milestone 5: Annotation Renderer

Tasks:

- **Live preview** (GPU for most types): Each annotation is a `QGraphicsItem` subclass added to `PreviewScene` as a child of the video item. The GPU composites them at render time.
  - `ArrowItem`, `RectItem`, `HighlightItem`, `TextItem` — drawn via `QPainter` in `paint()`, GPU-composited by the `QOpenGLWidget` viewport
  - `BlurItem` — **CPU path**: when the item is placed, crop the current frame's `QImage` sub-region, apply a stack-blur in software, store as a `QPixmap`. Display that static `QPixmap` in the scene. Do **not** attach a live `QGraphicsBlurEffect` — it re-rasterizes every frame on the CPU.
- Implement interactive handles as child `QGraphicsItem`s on each annotation item; use `QGraphicsItem::ItemIsMovable` and `ItemIsSelectable` flags
- **AnnotationStore** (service layer): holds the canonical `QList<Annotation>` data; serializes to JSON. The view layer `AnnotationLayer` syncs `QGraphicsItem` instances from this store — it does not own the data.
- **Export** (CPU): `ExportRenderer` walks `AnnotationStore`, and for each frame burns visible annotations into a `QImage` copy via `QPainter` before handing to the encoder
- Support undo/redo with a `QUndoStack`; commands modify `AnnotationStore` and signal `AnnotationLayer` to resync

### Milestone 6: MP4/WebM Export

Tasks:

- `RecordingStrategy` + `BufferedStrategy` already exist from Phase 1 Milestone 5.5
- Implement `StreamingStrategy` using ffmpeg subprocess (`QProcess`): pipe BGRA frames to `ffmpeg -f rawvideo -pix_fmt bgra -r {fps} -i pipe:0 -c:v libx264 output.mp4`
- `AppController::onStartRequested()` selects `BufferedStrategy` for GIF, `StreamingStrategy` for MP4/WebM — no other code changes needed
- After `StreamingStrategy::finish()` emits `encodingFinished(path)`, open the file in `PreviewController` via `QMediaPlayer` — same preview path as GIF
- Add export format selection to settings popover
- RAM note: GIF stays bounded by `BufferedStrategy`'s 30s soft limit; MP4 via `StreamingStrategy` is unbounded (disk-limited only)

## Phase 2 Monetization Options

Possible paid features:

- Advanced annotations
- High-quality GIF optimization
- MP4/WebM export
- No watermark, if a watermark is used
- Recent capture library

Recommended indie pricing:

- Free local recorder
- Pro one-time license: $15–$30
- Or Pro subscription: $5/month if continuous features are planned

Avoid being stingy in the free version. Trust is important.

---

# Phase 3 — Turn It Into a Product

Goal:

Move from local utility to paid workflow product.

This phase is where real SaaS revenue becomes possible.

## Phase 3 Product Features

### Instant Share Links

Workflow:

```text
Record → Stop → Upload → Link copied
```

The user should not have to think about file hosting.

### Cloud Library

Features:

- User account
- Capture history
- Rename recordings
- Organize into folders/projects
- Delete recordings
- Search recordings

### Privacy Controls

Per recording:

- Public link
- Private link
- Password protected
- Expiring link
- Disable download

### Team Workspaces

Features:

- Workspace recordings
- Shared folders
- Invite team members
- Role permissions

### Collaboration

Features:

- Comments
- Timeline comments
- Emoji reactions
- @mentions

### Analytics

Useful especially for demos/support:

- View count
- Watch duration
- Last viewed
- Drop-off points

### Integrations

Prioritize:

- Slack
- GitHub Issues
- Linear
- Jira
- Notion

Developer-focused integrations are likely a strong fit.

## Phase 3 Monetization

Suggested pricing:

### Free

- Local recording
- Limited cloud uploads or no cloud uploads
- Basic GIF export

### Pro — $8–$15/month

- Unlimited or larger upload quota
- Private links
- MP4/WebM export
- Advanced annotations
- Capture history

### Team — $20–$30/user/month

- Team workspace
- Admin controls
- Shared folders
- Comments
- Analytics
- Integrations

## Phase 3 Backend Notes

The cloud service can be separate from the desktop app.

Suggested backend concerns:

- Auth
- File upload
- Object storage
- CDN delivery
- Link permissions
- Team/workspace model
- Analytics events

Possible storage:

- S3-compatible object storage
- Cloudflare R2
- Backblaze B2

---

# Detailed UI/UX Flow

## Launch

On launch, show a capture frame centered on the primary monitor.

Default size:

```text
800 × 450
```

or remember the previous size.

Control bar appears attached to the bottom.

```text
● Ready | 30 FPS | 800×450 | Record | ⚙ | 📁 | ×
```

## Positioning

User can:

- Drag frame
- Resize frame
- Change FPS
- Open settings
- Start recording

Show dimensions while resizing:

```text
1024 × 768
```

## Start Recording

When user presses Record:

1. Optional countdown starts
2. Border changes to red
3. Timer starts
4. Control bar switches to recording mode

```text
⏺ Recording | Pause | Stop | 00:04
```

## During Recording

The capture area should generally be click-through so the user can interact with whatever is underneath.

The control bar remains interactive.

Optional recording overlays:

- Cursor
- Click ripple
- Keystroke display

## Stop Recording

When user presses Stop:

1. Capture thread stops
2. Frames are passed to encoder
3. Processing indicator appears
4. Preview mode opens automatically

Do not show a blocking save dialog by default.

## Preview Mode

Preview should autoplay and loop.

```text
▶  00:00 ━━━━━●━━━━━ 00:07

Trim | Annotate | Save | Copy | Share | Close
```

Primary actions:

- Copy
- Save
- Share later

The fastest path should be:

```text
Record → Stop → Copy → Paste
```

## Settings Popover

Keep settings small and contextual.

```text
Output
  GIF
  MP4

Recording FPS
  10  15  24  30  60

GIF Output FPS
  5   10  15

GIF Max Width
  480  640  800  Original

Quality
  Low  Medium  High

Capture
  Show cursor
  Show click effects
  Countdown

Hotkeys
  Start/Stop: Ctrl+Shift+R
```

Avoid a large settings window until the app needs it.

---

# Implementation Notes for Qt6

## Namespace

All application code lives in the `sc` namespace:

```cpp
namespace sc {

class AppController : public QObject { ... };
class CaptureRegion { ... };
// etc.

} // namespace sc
```

`main.cpp` is the only file that operates outside the namespace.

## Layered Architecture

The codebase is split into three layers. **Nothing in the view layer imports service layer headers.** The controller layer is the only bridge.

```text
┌─────────────────────────────────────────────────────────────────────┐
│  View layer  (main thread — pure display, no business logic)        │
│                                                                     │
│  CaptureWindow   QGraphicsView; owns border QGraphicsRectItem;     │
│                  calls AppController on drag/resize events          │
│  ControlBar      QWidget; emits button signals upward to            │
│                  AppController; never commands workers directly     │
│  PreviewScene    QGraphicsScene; owns QGraphicsVideoItem,           │
│                  AnnotationLayer, ScrubberOverlay; driven by        │
│                  PreviewController signals — no data logic          │
│  AnnotationLayer QGraphicsItem subclasses in the scene; receives    │
│                  add/remove/update from AppController via signals   │
│  SettingsPopover QWidget; reads/writes RecordingSettings struct;    │
│                  no state machine knowledge                         │
└───────────────────────────┬─────────────────────────────────────────┘
                            │ signals / slots only
┌───────────────────────────▼─────────────────────────────────────────┐
│  Controller layer  (main thread QObjects — orchestration only)      │
│                                                                     │
│  AppController   owns AppState machine; creates/destroys service    │
│                  objects; connects all signals; the only class      │
│                  that knows about both layers                       │
│  PreviewController drives PreviewScene playback; owns              │
│                  QMediaPlayer or manual QVideoSink; responds to     │
│                  scrub/play/pause/trim commands from ControlBar     │
└───────────────────────────┬─────────────────────────────────────────┘
                            │ signals / slots only
┌───────────────────────────▼─────────────────────────────────────────┐
│  Service layer  (worker threads — no widget or scene knowledge)     │
│                                                                     │
│  CaptureService  QObject on QThread; owns QScreenCapture,          │
│                  QMediaCaptureSession, QVideoSink; emits            │
│                  frameReady(QVideoFrame), progressUpdated,          │
│                  recordingFinished; thread-safe region setter       │
│  FrameStore      thread-safe buffer of QVideoFrame objects;        │
│                  written by CaptureService, read by encoders and   │
│                  PreviewController; also supplies QImage at         │
│                  encode-time via frame.toImage() — once, off-thread │
│  GifEncoder      QObject on QThread; reads FrameStore, encodes,    │
│                  emits encodingProgress(float), encodingFinished    │
│  Mp4Encoder      QObject on QThread; same interface as GifEncoder  │
│  AnnotationStore pure data: QList<Annotation> + JSON serialize;    │
│                  no QGraphicsItem knowledge                         │
│  ExportRenderer  burns AnnotationStore onto QImage frames via      │
│                  QPainter; called by encoders only                  │
└─────────────────────────────────────────────────────────────────────┘
```

The key rules:

- **Service → Controller**: via Qt signals only
- **Controller → View**: via Qt signals or direct method calls on view objects it owns
- **Service layer classes** have zero `#include` of any `QWidget`, `QGraphicsItem`, or `QGraphicsScene` header
- **View layer classes** have zero `#include` of any service layer header — they receive all data through controller-mediated signals

## Threading Model

All cross-thread communication is signals and slots. Workers never touch widgets.

```text
Main thread (UI + controllers)
  │
  ├─ AppController
  │    ├─ creates CaptureService on worker thread
  │    └─ creates GifEncoder / Mp4Encoder on worker thread
  │
  └─ PreviewController
       ├─ drives PreviewScene (view layer)
       └─ reads FrameStore by index for scrubbing

CaptureService (QThread)
  ├─ emits frameReady(QVideoFrame)  →  FrameStore::append()  [buffering]
  └─ emits frameReady(QVideoFrame)  →  live QVideoSink  [preview during recording]

FrameStore  (thread-safe, owned by AppController)
  ├─ read by PreviewController for frame-by-frame playback
  └─ read by GifEncoder / Mp4Encoder at encode time

GifEncoder / Mp4Encoder (QThread)
  ├─ calls FrameStore::frameAt(i).toImage()  [one CPU copy, off main thread]
  ├─ emits encodingProgress(float)
  └─ emits encodingFinished(QString outputPath)
```

Key signals:

```cpp
// CaptureService signals
void frameReady(QVideoFrame frame);
void progressUpdated(qint64 elapsedMs);
void recordingFinished();

// GifEncoder / Mp4Encoder signals
void encodingProgress(float percent);
void encodingFinished(QString outputPath);
void encodingFailed(QString error);
```

## GPU Rendering Pipeline

The goal is to keep frame data on the GPU from capture through display, minimizing CPU copies.

### Capture → Live Preview (recording in progress)

```text
QScreenCapture
  └─ QMediaCaptureSession
       └─ QVideoSink  (receives QVideoFrame — may be GPU texture via QRhi)
            └─ QGraphicsVideoItem  (renders directly from QVideoFrame texture)
```

Connect `QMediaCaptureSession` directly to the `QGraphicsVideoItem` as its video output. The scene renders the live feed without any CPU `QImage` copies.

### Recorded Frames → Encoded Output → Preview

```text
QVideoFrame per tick
  └─ RecorderWorker buffers QVideoFrame list (or maps to QImage for CPU encode)
       └─ EncoderWorker: QVideoFrame.toImage() only at encode time (CPU, off main thread)
            └─ GIF/MP4 output file
                 └─ QMediaPlayer → QGraphicsVideoItem  (GPU playback of encoded file)
```

### Preview Scene Layout

```
QGraphicsView (QOpenGLWidget viewport)
  └─ QGraphicsScene
       ├─ QGraphicsVideoItem          (video frames, GPU-composited)
       ├─ AnnotationLayer (Z+1)
       │    ├─ ArrowItem  (QGraphicsLineItem subclass)
       │    ├─ RectItem   (QGraphicsRectItem subclass)
       │    ├─ BlurItem   (QGraphicsPixmapItem — CPU-blurred pixmap, see note below)
       │    └─ TextItem   (QGraphicsTextItem subclass)
       └─ ScrubberOverlay (Z+2)
            ├─ QGraphicsLineItem  (playhead)
            └─ QGraphicsRectItem  (trim-range highlight)
```

Set the viewport for GPU compositing:

```cpp
auto* glWidget = new QOpenGLWidget;
QSurfaceFormat fmt;
fmt.setSamples(4);  // MSAA
glWidget->setFormat(fmt);
view->setViewport(glWidget);
view->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
```

### QGraphicsEffect and Blur — Important Caveat

`QGraphicsBlurEffect` and all `QGraphicsEffect` subclasses are **software-rendered on the CPU**. Qt rasterizes the affected item to an offscreen pixmap, applies the effect pixel-by-pixel in software, then uploads the result for compositing. This is not a GPU path.

For the blur/redact annotation tool:

- **Acceptable for Phase 1–2**: For small redact boxes (e.g., hiding a password field or email address), the blurred area is typically small and the CPU cost is negligible. Pre-compute a blurred `QPixmap` of the sub-region when the user places the `BlurItem`, and display that static pixmap — do not attach a live `QGraphicsBlurEffect` that re-runs every frame.
- **If real-time large-area blur is needed in a later phase**: Replace `BlurItem` with a `QOpenGLShaderProgram`-based custom `QGraphicsItem` that applies a Gaussian blur via a GLSL fragment shader. This is a meaningful scope increase and should be deferred.

All other annotation types (Arrow, Rect, Highlight, Text) are drawn via `QPainter` on the `QOpenGLWidget`-backed scene and are GPU-composited without issue.

### QRhiWidget (Qt 6.6+)

`QRhiWidget` is **not** a drop-in replacement for `QOpenGLWidget` as a `QGraphicsView` viewport — it is a standalone widget for custom `QRhi`-based rendering that owns its own render loop. Use it only if you replace the `QGraphicsView` + scene-graph approach entirely with a hand-rolled `QRhiWidget` subclass.

`QOpenGLWidget` remains the correct `QGraphicsView` viewport choice across all target Qt versions. On Qt 6.x, `QOpenGLWidget` drives rendering through `QRhi` internally anyway — on macOS it uses Metal, on Windows D3D11/D3D12, on Linux OpenGL/Vulkan — so `Qt6::OpenGL` and `Qt6::OpenGLWidgets` are required module dependencies even though raw OpenGL is not called directly at runtime.

## App State

```cpp
enum class AppState {
    Idle,
    Positioning,
    Countdown,
    Recording,
    Paused,
    Processing,
    Preview
};
```

## Capture Region

```cpp
struct CaptureRegion {
    QScreen* screen = nullptr;
    QRect rect;  // in logical pixels

    QString dimensionsString() const {
        return QString("%1\u00d7%2").arg(rect.width()).arg(rect.height());
    }
};
```

## Output Settings

```cpp
enum class OutputFormat { Gif, Mp4, WebM };
enum class QualityPreset { Low, Medium, High };

struct RecordingSettings {
    int captureFps = 30;               // frames actually captured per second
    OutputFormat format = OutputFormat::Gif;
    QualityPreset quality = QualityPreset::Medium;
    bool showCursor = true;
    bool showClicks = true;
    bool countdown = false;
    QString outputDir;  // from QStandardPaths::MoviesLocation
};

// Separate from RecordingSettings — GIF output is its own concern.
// AppController passes this to GifEncoder; it is not used by CaptureService.
struct GifExportSettings {
    int outputFps   = 10;    // must be <= captureFps; encoder decimates frames
    int maxWidth    = 800;   // 0 = no scaling; encoder scales proportionally
    QualityPreset quality = QualityPreset::Medium;  // palette size + dither
    // Target: file size uploadable to GitHub issues (~10 MB), Asana, GitLab, Slack
};
```

The frame decimation step is straightforward: if `captureFps = 30` and `outputFps = 10`, the encoder takes every 3rd frame from `FrameStore`. GIF centisecond timing means frame delay = `100 / outputFps` centiseconds per frame — keep `outputFps` to values that divide evenly into 100 (5, 10, 20, 25, 50) to avoid rounding errors in GIF players.

Settings are persisted with `QSettings` (uses plist on macOS, registry on Windows, INI on Linux).

## Source Layout

```text
src/
  main.cpp

  # Controller layer
  appcontroller.hpp/.cpp      # AppState machine; creates/connects all workers and views
  previewcontroller.hpp/.cpp  # drives PreviewScene playback; owns QMediaPlayer / QVideoSink

  # Service layer (no widget/scene headers)
  captureservice.hpp/.cpp     # QThread; QScreenCapture, QMediaCaptureSession, QVideoSink
  framestore.hpp/.cpp         # thread-safe QVideoFrame buffer; toImage() on demand
  annotationstore.hpp/.cpp    # QList<Annotation> data model + JSON serialize
  exportrenderer.hpp/.cpp     # QPainter burn of AnnotationStore onto QImage frames
  gifencoder.hpp/.cpp         # reads FrameStore, encodes animated GIF
  mp4encoder.hpp/.cpp         # reads FrameStore, QProcess ffmpeg or QMediaRecorder
  filestore.hpp/.cpp          # QFile save, QStandardPaths
  clipboardhelper.hpp/.cpp    # QClipboard wrapper
  settings.hpp/.cpp           # QSettings load/save for RecordingSettings

  # View layer (no service layer headers)
  ui/
    capturewindow.hpp/.cpp    # QGraphicsView; QGraphicsRectItem border; drag/resize events
    controlbar.hpp/.cpp       # QWidget; emits button signals only
    previewwindow.hpp/.cpp    # QGraphicsView + QOpenGLWidget viewport; owns PreviewScene
    previewscene.hpp/.cpp     # QGraphicsScene; QGraphicsVideoItem + annotation + scrubber
    annotationitem.hpp/.cpp   # QGraphicsItem subclasses: ArrowItem, RectItem, BlurItem, TextItem
    settingspopover.hpp/.cpp  # QWidget; reads/writes RecordingSettings
```

---

# Copilot-Friendly Task List

Use these as implementation prompts in VS Code/Copilot.

## Prompt 1 — App Skeleton

Create a C++ Qt6 desktop app using CMake with `Core`, `Gui`, `Widgets`, `Multimedia`, `MultimediaWidgets`, `OpenGL`, and `OpenGLWidgets`. Define an `AppState` enum with Idle, Positioning, Countdown, Recording, Paused, Processing, and Preview values. Create a frameless always-on-top `QGraphicsView` for the capture region: back it with a `QGraphicsScene` that holds a `QGraphicsRectItem` for the visible border. Create a second `QWidget` for the control bar docked below it. Add Record, Settings, Folder, and Close buttons, plus FPS and dimensions labels. Keep code modular with separate classes for the state controller, capture window (`QGraphicsView` subclass), control bar, and settings.

## Prompt 2 — Capture Region Model

Implement a `CaptureRegion` struct holding a `QScreen*` and a `QRect`. Add helpers for clamping the rect to the screen bounds, moving, resizing from edges/corners, and formatting dimensions as a string like `800×450`. Implement drag-to-move via `mousePressEvent`/`mouseMoveEvent` on the capture window. Add unit tests for clamp and resize logic using Qt Test.

## Prompt 3 — Settings Model

Implement `RecordingSettings` with FPS, `OutputFormat`, `QualityPreset`, showCursor, showClicks, countdown, and outputDir fields. Load and save settings using `QSettings`. Include defaults: 30 FPS, GIF output, Medium quality, showCursor enabled, showClicks enabled, countdown disabled, outputDir from `QStandardPaths::MoviesLocation`.

## Prompt 4 — Screen Recorder Interface

Define `CaptureService` as a `QObject` subclass that runs on a `QThread`. It should support start, stop, pause, and resume for a given `CaptureRegion` and `RecordingSettings`. It emits `frameReady(QVideoFrame)`, `progressUpdated(qint64 ms)`, and `recordingFinished()` signals. It also exposes a `QVideoSink*` that `AppController` can connect to a `QGraphicsVideoItem` for live preview during recording. The main thread must never block waiting for frames.

## Prompt 5 — QScreenCapture Prototype

Implement a capture prototype using `QScreenCapture` (Qt 6.5+) connected to a `QMediaCaptureSession`. Attach a `QVideoSink` to the session and handle `QVideoSink::videoFrameChanged` to receive `QVideoFrame` objects. Convert a single `QVideoFrame` to `QImage` via `QVideoFrame::toImage()`, crop it to a `CaptureRegion` rect, and save it as a PNG using `QImage::save()`. This class lives in the **service layer** — no `QWidget` or `QGraphicsItem` headers. Add error handling for platforms where screen recording permission is denied, and display a `QMessageBox` (called from `AppController`) with instructions for granting permission on macOS.

## Prompt 6 — CaptureService (Recording Worker)

Implement `CaptureService` as a `QObject` that runs on a `QThread`. Use a `QTimer` and `QElapsedTimer` to capture frames at the target FPS without drift. On every frame tick, read the **current** `CaptureRegion` rect via a thread-safe setter so that moving the capture window mid-recording follows the window. Grab the full screen via `QScreenCapture` + `QMediaCaptureSession` + `QVideoSink`, receive frames as `QVideoFrame`, and emit `frameReady(QVideoFrame)`. `AppController` connects this signal to two slots: one on `FrameStore` to buffer the frame, and one on the live preview `QVideoSink` (for `QGraphicsVideoItem`). `CaptureService` itself has no knowledge of either. Emit `progressUpdated` every second.

## Prompt 7 — GIF Encoder

Implement `GifEncoder` as a `QObject` on a `QThread`. It receives `FrameStore*`, `GifExportSettings`, and an output file path. The encoder pipeline is:

1. **Frame decimation**: step through `FrameStore` at intervals of `captureFps / outputFps`, calling `frame.toImage()` only on selected frames — one CPU copy per kept frame, done off the main thread.
2. **Scaling**: if `maxWidth > 0` and the frame is wider, scale proportionally using `QImage::scaled(maxWidth, ..., Qt::KeepAspectRatio, Qt::SmoothTransformation)`.
3. **Palette quantization**: compute a per-frame or global 256-colour palette based on `QualityPreset` (Low = 64 colours, Medium = 128, High = 256; dithering on for Medium/High).
4. **Frame delay**: `delay = 100 / outputFps` centiseconds — use values of `outputFps` that divide evenly into 100 (5, 10, 20, 25) to avoid per-player rounding drift.
5. **Encoding**: write frames via `giflib` (CMake `FetchContent`). Emit `encodingProgress(float)` after each frame. Emit `encodingFinished(QString outputPath, qint64 fileSizeBytes)` on completion.

The encoder has no knowledge of the UI or scene. Target output: ≤ 10 MB for compatibility with GitHub issues, GitLab, Asana, Slack, and similar services. Surface the resulting file size in the control bar after encoding so the user can re-export at lower quality if needed.

## Prompt 8 — Preview Mode

After encoding finishes, transition the app to Preview state. Create a `PreviewWindow` containing a `QGraphicsView`; call `view->setViewport(new QOpenGLWidget)` so all compositing is GPU-accelerated via Qt's internal `QRhi` layer (Metal on macOS, D3D on Windows, OpenGL/Vulkan on Linux — no raw OpenGL code needed). Add a `PreviewScene` (`QGraphicsScene` subclass) to the view. Inside the scene, add a `QGraphicsVideoItem` and connect it to a `QMediaPlayer` pointed at the encoded output file — this gives GPU-accelerated video playback with no CPU copies. For GIF files (which `QMediaPlayer` may not loop natively), drive a `QGraphicsPixmapItem` with a `QTimer` advancing through the frame buffer instead. Display output file size and duration metadata in the control bar. Add Copy, Save, and Close buttons. Implement Copy by extracting a frame via `QVideoSink` attached to the `QMediaPlayer` and writing it to `QGuiApplication::clipboard()`. Implement Save by opening `QFileDialog::getSaveFileName`.

## Prompt 9 — Click-Through Capture Window

Make the capture region `QGraphicsView` window click-through while recording. Set `Qt::WA_TransparentForMouseEvents` on the view widget when entering Recording state, and clear it when returning to Idle or Paused. The `QGraphicsRectItem` border color updates to red via `setPen()` — no repaint required. The control bar window must always remain interactive. Test on macOS (may require `NSWindow.ignoresMouseEvents` via a platform-specific Objective-C++ bridge) and Windows.

## Prompt 10 — Inline Trim Model

Implement a `TrimModel` class holding `trimStart` and `trimEnd` as frame indices. Add methods to convert between frame index, millisecond timestamp, and normalized 0–1 slider position. Enforce that `trimStart < trimEnd` and both are within the valid frame range. Add Qt Test unit tests for edge cases.

## Prompt 11 — Annotation Model

Implement annotation items as `QGraphicsItem` subclasses living in `PreviewScene` (view layer). Annotation *data* lives in `AnnotationStore` (service layer) as a `QList<Annotation>` — the two are separate:

**Service layer (`AnnotationStore`):**
- `Annotation` struct with `AnnotationKind`, `startTime`/`endTime`, `QRectF bounds`, `QColor`, `strokeWidth`, optional `QString text`
- Serialize/deserialize the full list to JSON via `QJsonDocument`
- `QUndoStack` commands modify `AnnotationStore` and emit a `changed()` signal; the view layer syncs from that signal

**View layer (`AnnotationLayer` items in `PreviewScene`):**
- `ArrowItem` extends `QGraphicsLineItem`: line + arrowhead polygon in `paint()`
- `RectItem` extends `QGraphicsRectItem`: resizable with corner handle child items
- `HighlightItem` extends `QGraphicsRectItem`: semi-transparent fill
- `BlurItem` extends `QGraphicsPixmapItem`: pre-computed blurred `QPixmap` of the sub-region at placement time — **no live `QGraphicsBlurEffect`** (see GPU pipeline notes)
- `TextItem` extends `QGraphicsTextItem`: background fill rect drawn in `paint()`

All items carry `startTime`/`endTime`; `PreviewScene` shows/hides them based on current playback position.

## Prompt 12 — Export With Annotations

When exporting, burn annotations into each `QImage` frame using `QPainter` before GIF/MP4 encoding. Walk the annotation item list; for each frame timestamp, determine which items are active and render them:

- Arrow: `QPainter::drawLine` + arrowhead polygon via `QPainter::drawPolygon`
- Rectangle/Highlight: `QPainter::drawRect` with fill alpha
- Blur/Redact: crop the `QImage` sub-region, apply a stack-blur pass (or `QImage` + `QPainter` blur kernel), composite back
- Text: `QPainter::drawText` with a filled background rect

The live preview uses GPU-composited `QGraphicsItem` subclasses (no image copies). Export is the only time annotations touch CPU `QImage` pixels. Keep export rendering deterministic and covered by Qt Test unit tests that compare rendered output against reference images.

---

# Recording Strategy Layer

## Why formats need different mid-recording behavior

GIF and MP4/WebM have fundamentally different RAM and latency profiles. Treating them the same way — buffer everything, encode after — works for GIF but breaks for longer recordings in other formats. The solution is a **recording strategy** interface that `AppController` selects at record-start time based on `OutputFormat`.

```text
ScreenCaptureWorker (emits QImage + CaptureRegion per frame)
         │
         ▼
RecordingStrategy  ◄──── selected by AppController based on OutputFormat
    ├─ BufferedStrategy   (GIF: accumulates QImage frames in FrameStore)
    └─ StreamingStrategy  (MP4/WebM: writes frames to disk incrementally)
         │
         ▼
Encoder (GifEncoder / Mp4Encoder / ffmpeg subprocess)
```

## BufferedStrategy (GIF)

Accumulates all `QImage` frames in `FrameStore` in RAM. Encoding begins after `recordingFinished()`. Trimming and frame re-ordering are possible because all frames are in memory.

**RAM budget:** ~800×450×4 bytes × 30fps × 30s = ~1.2 GB. Fine for short GIF recordings (< 30s). For longer recordings the user should be on MP4.

**Interface:**
```cpp
class BufferedStrategy : public RecordingStrategy {
    void onFrame(const QImage& img, const CaptureRegion& region) override;
    // → FrameStore::addFrame()
    void finish() override;
    // → launch GifEncoder thread
};
```

## StreamingStrategy (MP4/WebM)

Writes frames incrementally to a temp file during recording. No per-frame RAM accumulation. Handles arbitrarily long recordings (standup meetings, "shorts", bug reproducer videos).

Two viable implementations — choose at Phase 2 time:

**Option A — `QMediaRecorder` direct pipeline (preferred if Qt 6.8+):**
`QVideoFrameInput` → `QMediaCaptureSession` → `QMediaRecorder`. Frames are pushed as `QVideoFrame` (constructed from `QImage`) directly into Qt's H.264/HEVC encoder. No subprocess, no temp file management. Trim requires re-encoding a range after the fact.

**Option B — ffmpeg subprocess via `QProcess`:**
Pipe raw BGRA frames to `ffmpeg -f rawvideo -pix_fmt bgra ... -c:v libx264`. More portable, works across Qt versions, easier to control bitrate/codec. More process lifecycle complexity.

For preview after recording: `StreamingStrategy::finish()` finalizes the file and hands the path to `PreviewController`, which opens it with `QMediaPlayer` — same GPU playback path regardless of whether it was buffered or streamed.

## RecordingStrategy interface

```cpp
class RecordingStrategy : public QObject {
    Q_OBJECT
public:
    virtual void onFrame(const QImage& img, const CaptureRegion& region) = 0;
    virtual void finish() = 0;  // called when worker emits recordingFinished()

signals:
    void encodingProgress(float fraction);
    void encodingFinished(QString outputPath);
    void encodingFailed(QString reason);
};
```

`AppController` owns the strategy object. On `onStartRequested()` it constructs the right strategy, connects `ScreenCaptureWorker::frameReady` to `strategy->onFrame()`, and connects the strategy's signals back to the controller. The rest of `AppController` is format-agnostic.

## Implementation order

1. **Phase 1 Milestone 5.5 (next):** Introduce `RecordingStrategy` base + `BufferedStrategy` wrapping GIF. Pure refactor, no behavior change. Flush out threading/lifetime issues early.
2. **Phase 2 Milestone 6:** Implement `StreamingStrategy` with ffmpeg (Option B first — simpler), wire up for MP4/WebM output. `AppController` just switches which strategy it constructs.
3. **Later:** Swap Option B for Option A (`QVideoFrameInput`) once Qt 6.8 is the minimum target.

---

# Risks and Unknowns

## Click-Through Support

Qt's `WA_TransparentForMouseEvents` works well on Windows and macOS but may require platform-specific bridge code for full passthrough on macOS (`NSWindow.ignoresMouseEvents`).

Risk level: low-medium

Mitigation:

- Keep capture frame and control bar as separate windows
- Make only the capture frame click-through
- Test macOS first; add an Objective-C++ bridge if needed
- Treat Linux Wayland as a special case

## Screen Capture APIs

`QScreenCapture` requires Qt 6.5+ and the Qt Multimedia module. On macOS the user must grant screen recording permission; `QScreenCapture` does not prompt automatically — the app must detect and explain the failure.

Risk level: medium

Mitigation:

- Prototype capture in milestone 3 before any other UI work
- Fall back to `QScreen::grabWindow()` for simpler cases
- Show a `QMessageBox` with a link to System Settings > Privacy on macOS

## GIF Encoding

Qt does not ship a built-in animated GIF encoder. `QImageWriter` supports GIF read but not animated GIF write. GIF is also a fundamentally different beast from `QMediaRecorder`-supported formats — it has a 256-colour palette limit, centisecond frame timing, and no audio. It cannot share the same encoding pipeline as MP4/WebM.

The right model: `GifEncoder` and `Mp4Encoder` share a common signal interface (`encodingProgress`, `encodingFinished`, `encodingFailed`) but are entirely separate implementations. `AppController` holds an `EncoderBase*` pointer and never needs to know which one is active.

Risk level: medium

Mitigation:

- Use `giflib` (C, mature, widely packaged) via CMake `FetchContent`
- Keep `outputFps` to values that divide evenly into 100 (5, 10, 20, 25) to avoid centisecond rounding drift across GIF players
- Default `outputFps = 10`, `maxWidth = 800` — targets a file size that fits GitHub issues (~10 MB limit), Asana, GitLab, Slack upload limits
- Expose file size in the UI after encoding so the user can re-export at lower quality without re-recording

## Qt Multimedia Availability

`QScreenCapture` and `QMediaRecorder` require `Qt6::Multimedia` which may not be installed in all Qt distributions.

Risk level: low

Mitigation:

- Document required Qt version (6.5+) and modules in README
- Provide a fallback capture path using `QScreen::grabWindow()`

## Multi-Window Layout

Keeping the control bar snapped visually to the capture window requires tracking `moveEvent` and `resizeEvent` and repositioning the control bar manually.

Risk level: low

Mitigation:

- Centralize window positioning logic in `AppController`
- Handle screen boundary flipping (bar above vs below) in one place

---

# Suggested MVP Definition

The MVP is complete when:

- User can position a capture frame
- User can record a selected region
- User can interact with the captured app while recording
- User can stop recording
- User gets a GIF output
- User can preview the GIF
- User can save or copy the result

Do not add cloud, accounts, collaboration, or complex editing before this works smoothly.

---

# Product Positioning

Avoid positioning as merely:

> A screen recorder

Better positioning:

> The fastest way to show what you mean.

or:

> Record, explain, and share a screen region in seconds.

or for developer-focused positioning:

> Tiny screen captures for bugs, demos, and async dev work.

---

# Build & Test

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build
```

Required Qt6 components: `Core`, `Gui`, `Widgets`, `Multimedia`, `MultimediaWidgets`, `OpenGL`, `OpenGLWidgets`.

`Qt6::OpenGL` and `Qt6::OpenGLWidgets` are required even though Qt 6 routes `QOpenGLWidget` through `QRhi` internally — the modules still contain the widget classes and platform integration code.

Unit tests use `Qt6::Test`. Test targets cover: `CaptureRegion` resize/clamp logic, `TrimModel` math, `AnnotationModel` undo/redo, and `RecordingSettings` load/save.

---

# Build Order Recommendation

1. Qt6 CMake project setup
2. Frameless always-on-top `QGraphicsView` capture window
3. Control bar docked below capture window
4. Capture region model (drag, resize, clamp); `QGraphicsRectItem` border
5. Single screenshot crop with `QScreenCapture` + `QVideoSink`
6. Recording loop (QThread, QTimer, `QVideoFrame` buffer)
7. GIF export
8. GPU preview window (`QGraphicsView` + `QOpenGLWidget` + `QGraphicsVideoItem`)
9. Click-through capture window
10. Polish UX (countdown, hotkeys, settings persistence)
11. Inline trim (`TrimModel` + `QGraphicsScene` scrubber overlay)
12. Annotations (`QGraphicsItem` subclasses in preview scene)
13. MP4/WebM via QMediaRecorder or ffmpeg subprocess
14. Upload/share links

---

# Nice-to-haves

- **High-DPI GIF output**: GIF encoder currently divides physical crop dimensions by `dpr` to produce 1× logical output (e.g. 1277×752 on a 2× Retina display). A `GifExportSettings::hiRes` flag could skip the division and write at full physical resolution (2554×1504) for users who want maximum quality at the cost of file size.
- **Proper SCK window exclusion**: `NSWindowSharingNone` is respected by macOS screenshot (SCK with `excludingWindows:` filter) but NOT by Qt's `QScreenCapture` (which creates its SCK stream without an exclusion list). Workaround: the capture window border is hidden during recording. Proper fix requires replacing `QScreenCapture` with a direct SCK implementation using `[SCContentFilter initWithDisplay:excludingWindows:]` to exclude our overlay windows.

