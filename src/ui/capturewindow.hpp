#pragma once

#include "../appcontroller.hpp"

#include <QGraphicsView>
#include <QRect>

class QGraphicsScene;
class QGraphicsRectItem;
class QGraphicsSimpleTextItem;

namespace sc {

// The transparent, always-on-top overlay that shows the capture boundary.
// Built on QGraphicsView so state changes are zero-repaint setPen() calls
// on QGraphicsRectItem rather than manual paintEvent redraws.
//
// It stays click-through in all states so underlying apps remain interactive;
// movement is handled by CenterHandle.
class CaptureWindow : public QGraphicsView {
    Q_OBJECT

public:
    static constexpr int kBorderWidth  = 2;
    static constexpr int kMinDimension = 80;

    explicit CaptureWindow(QObject* controller, QWidget* parent = nullptr);

    // Returns the aspect ratio locked at record-start (width/height), or 0.0
    // if not currently locked. Used by ControlBar's resize grip.
    double lockedAspect() const { return m_lockedAspect; }

signals:
    void regionChanged(const QRect& rect);

public slots:
    void onStateChanged(sc::AppState state);
    void onRegionChanged(const sc::CaptureRegion& region);
    void flashGreen();

protected:
    void showEvent(QShowEvent* event) override;
    // These fire AFTER macOS has committed the geometry change, making
    // them reliable for notifying the control bar to reposition.
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;

private:
    QColor  borderColor() const;

    // Repositions and recolors all scene items to match the current
    // view geometry and AppState. Called on resize and state change.
    void updateSceneGeometry();

    QGraphicsScene*          m_scene        = nullptr;
    QGraphicsRectItem*       m_borderItem   = nullptr;
    QGraphicsSimpleTextItem* m_statusItem   = nullptr;  // bottom-left LCD readout
    QGraphicsSimpleTextItem* m_dimsItem     = nullptr;  // bottom-right LCD readout

    AppState m_state          = AppState::Idle;
    bool     m_flashActive    = false;

    // Programmatic geometry changes from AppController are applied under a
    // QSignalBlocker (see onRegionChanged) so resizeEvent/moveEvent don't
    // re-emit regionChanged and form a feedback loop — no manual guard flag.

    // When recording, resize is constrained to the aspect ratio captured at
    // record-start, creating a zoom effect. 0.0 = unlocked (not recording).
    double  m_lockedAspect = 0.0;
};

} // namespace sc
