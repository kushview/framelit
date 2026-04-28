#pragma once

#include "../appcontroller.hpp"

#include <QWidget>
#include <QRect>
#include <QPoint>

namespace sc {

// The transparent, always-on-top overlay that shows the capture boundary.
// While recording it becomes click-through; in Idle/Positioning it accepts
// mouse events for drag-to-move and edge/corner resize.
class CaptureWindow : public QWidget {
    Q_OBJECT

public:
    static constexpr int kBorderWidth  = 2;
    static constexpr int kHandleSize   = 8;
    static constexpr int kMinDimension = 80;

    explicit CaptureWindow(QObject* controller, QWidget* parent = nullptr);

signals:
    void regionChanged(const QRect& rect);

public slots:
    void onStateChanged(sc::AppState state);
    void onRegionChanged(const sc::CaptureRegion& region);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    // These fire AFTER macOS has committed the geometry change, making
    // them reliable for notifying the control bar to reposition.
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;

private:
    enum class HitZone {
        None,
        Body,
        EdgeTop, EdgeBottom, EdgeLeft, EdgeRight,
        CornerTL, CornerTR, CornerBL, CornerBR
    };

    HitZone hitTest(const QPoint& localPos) const;
    QColor  borderColor() const;

    AppState m_state = AppState::Idle;

    // When true, resizeEvent/moveEvent will not re-emit regionChanged.
    // Set while applying a programmatic geometry change from AppController
    // to avoid a signal feedback loop.
    bool m_suppressSignal = false;

    // Drag state
    bool     m_dragging  = false;
    HitZone  m_dragZone  = HitZone::None;
    QPoint   m_dragStart;      // global position at press
    QRect    m_rectAtPress;    // window geometry at press
};

} // namespace sc
