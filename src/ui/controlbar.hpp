#pragma once

#include "../appcontroller.hpp"

#include <QWidget>
#include <QRect>
#include <QTimer>

class QLabel;
class QPushButton;
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

    // When true the app's own windows are NOT excluded from the SCK capture,
    // so the capture frame / control bar appear in the recorded output.
    bool demoMode() const;

signals:
    void startRequested();
    void stopRequested();
    void pauseRequested();
    void resumeRequested();

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

    CaptureWindow* m_captureWindow = nullptr;
    QTimer*        m_snapTimer     = nullptr;

    bool   m_dragging    = false;
    QPoint m_dragStart;        // global cursor position at press
    QPoint m_captureOrigin;    // capture window top-left at press

    QLabel*      m_statusLabel     = nullptr;
    QLabel*      m_dimensionsLabel = nullptr;
    QPushButton* m_recordButton    = nullptr;
    QPushButton* m_pauseButton     = nullptr;
    QPushButton* m_stopButton      = nullptr;
    QPushButton* m_demoButton      = nullptr;
    QPushButton* m_closeButton     = nullptr;

    AppState m_state = AppState::Idle;
};

} // namespace sc
