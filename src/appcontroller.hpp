#pragma once

#include <QObject>
#include <QRect>
#include <QScreen>
#include <QString>

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
};

struct RecordingSettings {
    int fps           = 30;
    OutputFormat format  = OutputFormat::Gif;
    QualityPreset quality = QualityPreset::Medium;
    bool showCursor   = true;
    bool showClicks   = true;
    bool countdown    = false;
    QString outputDir;
};

// ---------------------------------------------------------------------------
// AppController
// ---------------------------------------------------------------------------

class CaptureWindow;
class ControlBar;

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

signals:
    void stateChanged(sc::AppState newState);
    void regionChanged(const sc::CaptureRegion& region);

private:
    void setState(AppState s);
    void loadSettings();
    void saveSettings();

    AppState m_state = AppState::Idle;
    CaptureRegion m_region;
    RecordingSettings m_settings;

    CaptureWindow* m_captureWindow = nullptr;
    ControlBar*    m_controlBar    = nullptr;
};

} // namespace sc
