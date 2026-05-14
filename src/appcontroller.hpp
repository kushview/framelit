#pragma once

#include <QObject>
#include <QRect>
#include <QScreen>
#include <QSettings>
#include <QSize>
#include <QStandardPaths>
#include <QString>
#include <QTimer>

#ifdef Q_OS_MACOS
#include "globalhotkeys.hpp"
#endif

// Forward-declare Qt class outside namespace sc to avoid sc::QThread ambiguity
class QThread;

namespace sc {

// ---------------------------------------------------------------------------
// Core types
// ---------------------------------------------------------------------------

enum class AppState {
  Idle,
  Positioning,
  Countdown,
  Recording,
  Paused,
  Processing,
  Preview
};

enum class OutputFormat { Gif, Mp4, WebM };
enum class QualityPreset { Low, Medium, High };

struct CaptureRegion {
  QScreen *screen = nullptr;
  QRect rect;

  QString dimensionsString() const {
    return QString("%1×%2").arg(rect.width()).arg(rect.height());
  }

  bool isValid() const { return screen != nullptr && !rect.isEmpty(); }

  // Returns a new CaptureRegion whose rect is fully contained within
  // screenBounds. If rect is larger than bounds it is shrunk to fit.
  // The screen pointer is preserved.
  CaptureRegion clampedTo(const QRect &screenBounds) const {
    int w = qMin(rect.width(), screenBounds.width());
    int h = qMin(rect.height(), screenBounds.height());
    // Use x()+width() not right() — QRect::right() is left+width-1 (off-by-one)
    int x = qBound(screenBounds.x(), rect.left(),
                   screenBounds.x() + screenBounds.width() - w);
    int y = qBound(screenBounds.y(), rect.top(),
                   screenBounds.y() + screenBounds.height() - h);
    return CaptureRegion{screen, QRect(x, y, w, h)};
  }
};

struct RecordingSettings {
  int fps = 30;
  OutputFormat format = OutputFormat::Gif;
  QualityPreset quality = QualityPreset::Medium;
  bool showCursor = true;
  bool showClicks = true;
  bool countdown = false;
  bool captureAudio = false; // mic audio muxed into MP4; no effect on GIF
  bool hiDpi = false; // 2× output resolution — multiplies outputSize by 2
  bool letterbox =
      true; // letterbox/pillarbox to preserve aspect; false = stretch to fill
  bool demoMode =
      false; // when true, app windows are visible to external screen recorders
  QSize outputSize = {800, 450}; // base output size; doubled when hiDpi is on
  QString audioDeviceId;         // empty = system default (input)
  QString audioOutputDeviceId;   // empty = system default (output)
  QString outputDir;
  int growStep = 10; // px added/removed per grow/shrink hotkey press

  // Populate from QSettings. Any key not present keeps its default value.
  static RecordingSettings load(QSettings &qs) {
    RecordingSettings s;
    s.fps = qs.value("fps", s.fps).toInt();
    s.format = static_cast<OutputFormat>(
        qs.value("format", static_cast<int>(s.format)).toInt());
    s.quality = static_cast<QualityPreset>(
        qs.value("quality", static_cast<int>(s.quality)).toInt());
    s.showCursor = qs.value("showCursor", s.showCursor).toBool();
    s.showClicks = qs.value("showClicks", s.showClicks).toBool();
    s.countdown = qs.value("countdown", s.countdown).toBool();
    s.captureAudio = qs.value("captureAudio", s.captureAudio).toBool();
    s.hiDpi = qs.value("hiDpi", s.hiDpi).toBool();
    s.outputSize =
        QSize(qs.value("outputSizeW", s.outputSize.width()).toInt(),
              qs.value("outputSizeH", s.outputSize.height()).toInt());
    s.audioDeviceId = qs.value("audioDeviceId", s.audioDeviceId).toString();
    s.audioOutputDeviceId =
        qs.value("audioOutputDeviceId", s.audioOutputDeviceId).toString();
    s.letterbox = qs.value("letterbox", s.letterbox).toBool();
    s.demoMode = qs.value("demoMode", s.demoMode).toBool();
    s.outputDir = qs.value("outputDir", QStandardPaths::writableLocation(
                                            QStandardPaths::MoviesLocation))
                      .toString();
    s.growStep = qs.value("growStep", s.growStep).toInt();
    return s;
  }

  void save(QSettings &qs) const {
    qs.setValue("fps", fps);
    qs.setValue("format", static_cast<int>(format));
    qs.setValue("quality", static_cast<int>(quality));
    qs.setValue("showCursor", showCursor);
    qs.setValue("showClicks", showClicks);
    qs.setValue("countdown", countdown);
    qs.setValue("captureAudio", captureAudio);
    qs.setValue("hiDpi", hiDpi);
    qs.setValue("outputSizeW", outputSize.width());
    qs.setValue("outputSizeH", outputSize.height());
    qs.setValue("audioDeviceId", audioDeviceId);
    qs.setValue("audioOutputDeviceId", audioOutputDeviceId);
    qs.setValue("letterbox", letterbox);
    qs.setValue("demoMode", demoMode);
    qs.setValue("outputDir", outputDir);
    qs.setValue("growStep", growStep);
  }
};

// ---------------------------------------------------------------------------
// AppController
// ---------------------------------------------------------------------------

class CaptureWindow;
class CenterHandle;
class CloseButton;
class ControlBar;
class EditWindow;
class MainMenu;
class RecorderWorker;
class RecordingStrategy;
class SystemTray;
class Actions;

class AppController : public QObject {
  Q_OBJECT

public:
  explicit AppController(QObject *parent = nullptr);
  ~AppController() override;

  void start();

  AppState state() const { return m_state; }
  const CaptureRegion &captureRegion() const { return m_region; }
  const RecordingSettings &settings() const { return m_settings; }

public slots:
  void onStartRequested();
  void onStopRequested();
  void onPauseRequested();
  void onResumeRequested();
  void onRegionChanged(const QRect &rect);
  void onRecordingFinished();
  void onProgressUpdated(qint64 elapsedMs);
  void onCaptureError(const QString &message);
  void onEncodingProgress(float fraction);
  void onEncodingFinished(const QString &filePath);
  void onEncodingFailed(const QString &reason);
  void onFormatChangeRequested(sc::OutputFormat format);
  void onAudioChangeRequested(bool captureAudio);
  void onHiDpiChangeRequested(bool hiDpi);
  void onDemoModeChangeRequested(bool on);
  void onAudioDeviceChangeRequested(const QString &deviceId);
  void onAudioOutputDeviceChangeRequested(const QString &deviceId);
  void onOutputDirChangeRequested(const QString &dir);
  void onOutputSizeChangeRequested(QSize size);
  void onGrowStepChangeRequested(int step);
  void onFollowMouseChangeRequested(bool enabled);
  void onFollowMouseToggleRequested();
  void onLetterboxChangeRequested(bool letterbox);
  void onRecordToggleRequested();
  void onFollowMouseTick();
  void onSnapAspectRequested();
  void onPreferencesRequested();
  void onOpenOutputDirRequested();
  void onGrowRequested();
  void onShrinkRequested();
  void onScreenshotRequested();
  void onPreviewClosed();
  void onOpenPreviewRequested();
  void onPreviewToggleRequested(bool show);

signals:
  void stateChanged(sc::AppState newState);
  void regionChanged(const sc::CaptureRegion &region);
  // Relayed from the worker; control bar uses this to update its timer label.
  void recordingProgress(qint64 elapsedMs);

private:
  void setState(AppState s);
  void loadSettings();
  void saveSettings();
  void applyDemoMode();
  // Attach a concrete worker: moves it to a dedicated QThread and wires
  // signals. Takes ownership of both worker and the thread.
  void attachWorker(RecorderWorker *worker);
  void teardownWorker();
  void applySettingsToUI();
  void applyResizeDelta(int delta);
  void updateFollowTimer(); // start/stop m_followTimer based on state + flag
  void syncCenterHandleVisibility();
  void setUiVisible(bool visible);
  void toggleUiVisible();
  void syncActions();           // calls m_actions->sync() with current state
  void openPreferencesDialog(); // create, connect, and exec PreferencesDialog

  AppState m_state = AppState::Idle;
  CaptureRegion m_region;
  RecordingSettings m_settings;

  double m_resizeAspect = 0.0; // latched on first hotkey resize; 0 = unset
  bool m_applyingResize =
      false; // true while applyResizeDelta is calling onRegionChanged
  bool m_followMouse = false; // follow-mouse pan mode
  bool m_previewVisible = false; // canonical preview visibility for UI sync

  CaptureWindow *m_captureWindow = nullptr;
  CenterHandle *m_centerHandle = nullptr;
  CloseButton *m_closeButton = nullptr;
  ControlBar *m_controlBar = nullptr;
  EditWindow *m_editWindow = nullptr;
  MainMenu *m_mainMenu = nullptr;
  RecorderWorker *m_worker = nullptr;
  QThread *m_workerThread = nullptr;
  RecordingStrategy *m_strategy = nullptr; // owned; created per recording
  QMetaObject::Connection
      m_frameConn; // frameReady→onFrame; disconnected before strategy teardown
  QTimer *m_followTimer = nullptr;
  SystemTray *m_tray = nullptr;
  Actions *m_actions = nullptr;
  QString m_lastOutputPath;

#ifdef Q_OS_MACOS
  GlobalHotkeys *m_hotkeyManager = nullptr;
#endif
};

} // namespace sc
