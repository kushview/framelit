#pragma once

#include <QObject>
#include <QRect>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QString>

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
    QScreen* screen = nullptr;
    QRect rect;

    QString dimensionsString() const {
        return QString("%1×%2").arg(rect.width()).arg(rect.height());
    }

    bool isValid() const {
        return screen != nullptr && !rect.isEmpty();
    }

    // Returns a new CaptureRegion whose rect is fully contained within
    // screenBounds. If rect is larger than bounds it is shrunk to fit.
    // The screen pointer is preserved.
    CaptureRegion clampedTo(const QRect& screenBounds) const
    {
        int w = qMin(rect.width(),  screenBounds.width());
        int h = qMin(rect.height(), screenBounds.height());
        // Use x()+width() not right() — QRect::right() is left+width-1 (off-by-one)
        int x = qBound(screenBounds.x(), rect.left(), screenBounds.x() + screenBounds.width()  - w);
        int y = qBound(screenBounds.y(), rect.top(),  screenBounds.y() + screenBounds.height() - h);
        return CaptureRegion{ screen, QRect(x, y, w, h) };
    }
};

struct RecordingSettings {
    int fps              = 30;
    OutputFormat  format  = OutputFormat::Gif;
    QualityPreset quality = QualityPreset::Medium;
    bool showCursor      = true;
    bool showClicks      = true;
    bool countdown       = false;
    QString outputDir;

    // Populate from QSettings. Any key not present keeps its default value.
    static RecordingSettings load(QSettings& qs)
    {
        RecordingSettings s;
        s.fps        = qs.value("fps",        s.fps).toInt();
        s.format     = static_cast<OutputFormat>(
                           qs.value("format", static_cast<int>(s.format)).toInt());
        s.quality    = static_cast<QualityPreset>(
                           qs.value("quality", static_cast<int>(s.quality)).toInt());
        s.showCursor = qs.value("showCursor", s.showCursor).toBool();
        s.showClicks = qs.value("showClicks", s.showClicks).toBool();
        s.countdown  = qs.value("countdown",  s.countdown).toBool();
        s.outputDir  = qs.value("outputDir",
            QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)).toString();
        return s;
    }

    void save(QSettings& qs) const
    {
        qs.setValue("fps",        fps);
        qs.setValue("format",     static_cast<int>(format));
        qs.setValue("quality",    static_cast<int>(quality));
        qs.setValue("showCursor", showCursor);
        qs.setValue("showClicks", showClicks);
        qs.setValue("countdown",  countdown);
        qs.setValue("outputDir",  outputDir);
    }
};

// ---------------------------------------------------------------------------
// AppController
// ---------------------------------------------------------------------------

class CaptureWindow;
class ControlBar;
class RecorderWorker;
class FrameStore;

class AppController : public QObject {
    Q_OBJECT

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    void start();

    AppState state() const { return m_state; }
    const CaptureRegion& captureRegion() const { return m_region; }
    const RecordingSettings& settings() const { return m_settings; }

public slots:
    void onStartRequested();
    void onStopRequested();
    void onPauseRequested();
    void onResumeRequested();
    void onRegionChanged(const QRect& rect);
    void onRecordingFinished();
    void onProgressUpdated(qint64 elapsedMs);
    void onCaptureError(const QString& message);

signals:
    void stateChanged(sc::AppState newState);
    void regionChanged(const sc::CaptureRegion& region);
    // Relayed from the worker; control bar uses this to update its timer label.
    void recordingProgress(qint64 elapsedMs);

private:
    void setState(AppState s);
    void loadSettings();
    void saveSettings();
    // Attach a concrete worker: moves it to a dedicated QThread and wires signals.
    // Takes ownership of both worker and the thread.
    void attachWorker(RecorderWorker* worker);
    void teardownWorker();

    AppState m_state = AppState::Idle;
    CaptureRegion m_region;
    RecordingSettings m_settings;

    CaptureWindow*  m_captureWindow  = nullptr;
    ControlBar*     m_controlBar     = nullptr;
    RecorderWorker* m_worker         = nullptr;
    QThread*        m_workerThread   = nullptr;
    FrameStore*     m_frameStore     = nullptr;
};

} // namespace sc
