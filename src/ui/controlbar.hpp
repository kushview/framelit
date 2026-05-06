#pragma once

#include "../appcontroller.hpp"

#include <QWidget>
#include <QRect>
#include <QSize>
#include <QTimer>

class QLabel;
class QPushButton;
class QComboBox;
class QHBoxLayout;

namespace sc {

class CaptureWindow;

// The docked control bar window. Always on top, interactive (never click-through).
// Snaps to the bottom edge of the capture region window, flipping above if
// there is not enough space below.
class ControlBar : public QWidget {
    Q_OBJECT

public:
    explicit ControlBar(CaptureWindow* captureWindow, QWidget* parent = nullptr);

    void snapToRegion(const QRect& captureRect);

    // Restore the audio device combo selection from a saved ID.
    // Pass an empty string (or an ID that no longer exists) to select the
    // system default (index 0).  Does not emit audioDeviceChangeRequested.
    void setAudioDeviceId(const QString& id);
    void setOutputDir(const QString& dir);
    void setOutputSize(QSize size);
    void setGrowStep(int step);
    void setFormat(sc::OutputFormat format);
    void setHiDpi(bool hiDpi);
    void setCaptureAudio(bool on);
    void setFollowMouse(bool enabled);
    void setLetterbox(bool letterbox);
    void setDemoMode(bool on);

signals:
    void startRequested();
    void stopRequested();
    void pauseRequested();
    void resumeRequested();
    void formatChangeRequested(sc::OutputFormat format);
    void audioChangeRequested(bool captureAudio);
    void audioDeviceChangeRequested(QString deviceId);
    void hiDpiChangeRequested(bool hiDpi);
    void followMouseChangeRequested(bool enabled);
    void outputDirChangeRequested(QString dir);
    void outputSizeChangeRequested(QSize size);
    void growStepChangeRequested(int step);
    void letterboxChangeRequested(bool letterbox);
    void demoModeChangeRequested(bool on);
    void snapAspectRequested();
    void preferencesRequested();

public slots:
    void onStateChanged(sc::AppState state);
    void onRegionChanged(const sc::CaptureRegion& region);

private:
    void buildUi();
    void updateUiForState(AppState state);

    // Drag the whole apparatus by clicking the bar background
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void showEvent(QShowEvent* event) override;

    bool isInGripZone(const QPoint& localPos) const;

    CaptureWindow* m_captureWindow = nullptr;
    QTimer*        m_snapTimer     = nullptr;

    // Bar-background drag (moves the whole apparatus)
    bool   m_dragging    = false;
    QPoint m_dragStart;
    QPoint m_captureOrigin;

    // Resize-grip drag (bottom-right corner of the bar)
    bool   m_resizing          = false;
    QRect  m_captureRectAtPress;

    static constexpr int kGripSize = 18;  // px square of the resize hit zone

    QPushButton* m_formatButton    = nullptr;
    QPushButton* m_audioButton     = nullptr;
    QComboBox*   m_audioDeviceCombo = nullptr;
    QPushButton* m_recordButton    = nullptr;
    QPushButton* m_pauseButton     = nullptr;
    QPushButton* m_stopButton      = nullptr;
    QPushButton* m_snapButton      = nullptr;
    QPushButton* m_settingsButton  = nullptr;
    QPushButton* m_hiDpiButton     = nullptr;
    QPushButton* m_followMouseButton = nullptr;

    OutputFormat m_format      = OutputFormat::Gif;
    bool         m_captureAudio = false;
    bool         m_hiDpi        = false;
    bool         m_followMouse  = false;
    bool         m_letterbox    = true;
    bool         m_demoMode     = false;
    AppState m_state = AppState::Idle;
    QString      m_outputDir;
    QSize        m_outputSize = {800, 450};
    int          m_growStep   = 10;
};

} // namespace sc
