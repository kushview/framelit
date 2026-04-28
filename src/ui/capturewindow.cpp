#include "capturewindow.hpp"

#include <QColor>
#include <QCursor>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>

namespace sc {

CaptureWindow::CaptureWindow(QObject* /*controller*/, QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint
                 | Qt::WindowStaysOnTopHint
                 | Qt::Tool);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true); // receive mouseMoveEvent without a button held (cursor shape updates)

    // Default geometry — AppController will push the real region via slot
    setGeometry(100, 100, 800, 450);
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void CaptureWindow::onStateChanged(sc::AppState state)
{
    m_state = state;

    // Enable click-through while recording so the user can interact with
    // whatever is underneath the capture region.
    bool passthrough = (state == AppState::Recording);
    setAttribute(Qt::WA_TransparentForMouseEvents, passthrough);

    update(); // repaint border color
}

void CaptureWindow::onRegionChanged(const sc::CaptureRegion& region)
{
    // Suppress re-emission from resizeEvent/moveEvent so we don't loop
    // back through AppController when geometry is set programmatically.
    m_suppressSignal = true;
    setGeometry(region.rect);
    m_suppressSignal = false;
}

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------

void CaptureWindow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    // Alpha=1 (not zero) so macOS delivers mouse events to the interior.
    // Visually this is imperceptible (0.4% opacity).
    p.fillRect(rect(), QColor(0, 0, 0, 1));

    // Colored border
    QPen pen(borderColor(), kBorderWidth);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    QRect borderRect = rect().adjusted(kBorderWidth / 2, kBorderWidth / 2,
                                      -kBorderWidth / 2, -kBorderWidth / 2);
    p.drawRect(borderRect);

    // Resize handles (small filled squares at corners and edge midpoints)
    if (m_state == AppState::Idle || m_state == AppState::Positioning) {
        p.setBrush(borderColor());
        p.setPen(Qt::NoPen);
        const int h  = kHandleSize;
        const int h2 = h / 2;
        int w = width(), ht = height();
        // Corners
        p.drawRect(0,          0,           h, h);
        p.drawRect(w - h,      0,           h, h);
        p.drawRect(0,          ht - h,      h, h);
        p.drawRect(w - h,      ht - h,      h, h);
        // Edge midpoints
        p.drawRect(w / 2 - h2, 0,           h, h);
        p.drawRect(w / 2 - h2, ht - h,      h, h);
        p.drawRect(0,          ht / 2 - h2, h, h);
        p.drawRect(w - h,      ht / 2 - h2, h, h);
    }
}

// ---------------------------------------------------------------------------
// Mouse interaction
// ---------------------------------------------------------------------------

CaptureWindow::HitZone CaptureWindow::hitTest(const QPoint& pos) const
{
    const int h = kHandleSize;
    const int w = width(), ht = height();

    bool onLeft   = pos.x() < h;
    bool onRight  = pos.x() > w - h;
    bool onTop    = pos.y() < h;
    bool onBottom = pos.y() > ht - h;

    if (onTop    && onLeft)  return HitZone::CornerTL;
    if (onTop    && onRight) return HitZone::CornerTR;
    if (onBottom && onLeft)  return HitZone::CornerBL;
    if (onBottom && onRight) return HitZone::CornerBR;
    if (onTop)               return HitZone::EdgeTop;
    if (onBottom)            return HitZone::EdgeBottom;
    if (onLeft)              return HitZone::EdgeLeft;
    if (onRight)             return HitZone::EdgeRight;
    return HitZone::Body;
}

void CaptureWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    m_dragZone    = hitTest(event->pos());
    m_dragging    = (m_dragZone != HitZone::None);
    m_dragStart   = event->globalPosition().toPoint();
    m_rectAtPress = geometry();
}

void CaptureWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging) {
        // Update cursor shape based on hover zone
        switch (hitTest(event->pos())) {
        case HitZone::CornerTL: case HitZone::CornerBR: setCursor(Qt::SizeFDiagCursor); break;
        case HitZone::CornerTR: case HitZone::CornerBL: setCursor(Qt::SizeBDiagCursor); break;
        case HitZone::EdgeTop:  case HitZone::EdgeBottom: setCursor(Qt::SizeVerCursor);  break;
        case HitZone::EdgeLeft: case HitZone::EdgeRight:  setCursor(Qt::SizeHorCursor);  break;
        case HitZone::Body: setCursor(Qt::SizeAllCursor); break;
        default: setCursor(Qt::ArrowCursor); break;
        }
        return;
    }

    QPoint delta = event->globalPosition().toPoint() - m_dragStart;
    QRect r = m_rectAtPress;

    switch (m_dragZone) {
    case HitZone::Body:
        r.translate(delta);
        break;
    case HitZone::EdgeTop:
        r.setTop(qMin(r.top() + delta.y(), r.bottom() - kMinDimension));
        break;
    case HitZone::EdgeBottom:
        r.setBottom(qMax(r.bottom() + delta.y(), r.top() + kMinDimension));
        break;
    case HitZone::EdgeLeft:
        r.setLeft(qMin(r.left() + delta.x(), r.right() - kMinDimension));
        break;
    case HitZone::EdgeRight:
        r.setRight(qMax(r.right() + delta.x(), r.left() + kMinDimension));
        break;
    case HitZone::CornerTL:
        r.setTopLeft(r.topLeft() + delta);
        if (r.width()  < kMinDimension) r.setLeft(r.right()  - kMinDimension);
        if (r.height() < kMinDimension) r.setTop(r.bottom()  - kMinDimension);
        break;
    case HitZone::CornerTR:
        r.setTopRight(r.topRight() + delta);
        if (r.width()  < kMinDimension) r.setRight(r.left()  + kMinDimension);
        if (r.height() < kMinDimension) r.setTop(r.bottom()  - kMinDimension);
        break;
    case HitZone::CornerBL:
        r.setBottomLeft(r.bottomLeft() + delta);
        if (r.width()  < kMinDimension) r.setLeft(r.right()  - kMinDimension);
        if (r.height() < kMinDimension) r.setBottom(r.top()  + kMinDimension);
        break;
    case HitZone::CornerBR:
        r.setBottomRight(r.bottomRight() + delta);
        if (r.width()  < kMinDimension) r.setRight(r.left()  + kMinDimension);
        if (r.height() < kMinDimension) r.setBottom(r.top()  + kMinDimension);
        break;
    default: break;
    }

    setGeometry(r);
    // Do NOT emit regionChanged here — resizeEvent/moveEvent will emit
    // after macOS has committed the geometry, which is what the control
    // bar needs to reposition reliably.
}

void CaptureWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_dragging = false;
    m_dragZone = HitZone::None;
}

// resizeEvent and moveEvent fire after macOS has committed the window
// geometry change to the compositor — unlike our synchronous emit inside
// mouseMoveEvent, these are guaranteed to carry the actual new geometry.
void CaptureWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (!m_suppressSignal)
        emit regionChanged(geometry());
}

void CaptureWindow::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    if (!m_suppressSignal)
        emit regionChanged(geometry());
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QColor CaptureWindow::borderColor() const
{
    switch (m_state) {
    case AppState::Recording:  return QColor(0xEF, 0x44, 0x44); // red
    case AppState::Paused:     return QColor(0xFA, 0xCC, 0x15); // yellow
    default:                   return QColor(0x94, 0xA3, 0xB8); // slate
    }
}

} // namespace sc
