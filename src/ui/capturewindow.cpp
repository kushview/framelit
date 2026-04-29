#include "capturewindow.hpp"

#include <QColor>
#include <QCursor>
#include <QFont>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QMouseEvent>
#include <QPen>
#include <QTimer>

#ifdef Q_OS_MACOS
#include "../platform/macos_window.h"
#endif

namespace sc {

CaptureWindow::CaptureWindow(QObject* /*controller*/, QWidget* parent)
    : QGraphicsView(parent)
{
    setWindowFlags(Qt::FramelessWindowHint
                 | Qt::WindowStaysOnTopHint
                 | Qt::Tool);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);

    // The QGraphicsView has an internal viewport widget — both need
    // transparency set so macOS composites the window correctly.
    viewport()->setAttribute(Qt::WA_TranslucentBackground, true);
    viewport()->setAttribute(Qt::WA_NoSystemBackground, true);

    // No frame, no scroll bars — the view IS the window.
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // All mouse handling is at the view level; items are display-only.
    setInteractive(false);

    // Near-zero alpha so macOS still delivers mouse events to this window
    // even over the visually transparent interior. 1/255 ≈ 0.4% opacity,
    // imperceptible to the eye.
    setBackgroundBrush(QColor(0, 0, 0, 1));

    // Scene — rect is kept in sync with the view size via updateSceneGeometry()
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);

    // Border rectangle
    m_borderItem = m_scene->addRect(QRectF(),
                                    QPen(borderColor(), kBorderWidth),
                                    Qt::NoBrush);
    m_borderItem->setZValue(1);

    // Dimension label (e.g. "800×450") shown in the top-left corner
    m_labelItem = m_scene->addSimpleText(QString());
    QFont labelFont;
    labelFont.setPointSize(9);
    m_labelItem->setFont(labelFont);
    m_labelItem->setBrush(borderColor());
    m_labelItem->setZValue(2);

    // 8 resize handles: corners (0-3) then edge midpoints (4-7)
    for (int i = 0; i < 8; ++i) {
        m_handles[i] = m_scene->addRect(QRectF(), Qt::NoPen, QBrush(borderColor()));
        m_handles[i]->setZValue(2);
    }

    setGeometry(100, 100, 800, 450);
    updateSceneGeometry();
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

    // Update border/handle colors and handle visibility — no repaint needed;
    // QGraphicsItem::setPen/setBrush triggers only the affected item's redraw.
    updateSceneGeometry();
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
// Scene layout
// ---------------------------------------------------------------------------

void CaptureWindow::updateSceneGeometry()
{
    const qreal w  = width();
    const qreal ht = height();
    const qreal h  = kHandleSize;
    const qreal bw = kBorderWidth;

    setSceneRect(0, 0, w, ht);

    // Border
    m_borderItem->setRect(QRectF(bw / 2.0, bw / 2.0, w - bw, ht - bw));
    m_borderItem->setPen(QPen(borderColor(), bw));

    // Dimension label — color follows state, positioned just inside top-left corner
    const QColor bc = borderColor();
    m_labelItem->setBrush(bc);
    m_labelItem->setText(QString("%1\xc3\x97%2").arg(int(w)).arg(int(ht)));
    m_labelItem->setPos(bw + 4, bw + 4);

    // Resize handles: visible only when the user can interact with the window
    const bool showHandles = (m_state == AppState::Idle ||
                              m_state == AppState::Positioning);
    const QBrush handleBrush(bc);

    // Index order: TL, TR, BL, BR, EdgeTop, EdgeBottom, EdgeLeft, EdgeRight
    const QPointF positions[8] = {
        {0,             0            },  // 0: CornerTL
        {w - h,         0            },  // 1: CornerTR
        {0,             ht - h       },  // 2: CornerBL
        {w - h,         ht - h       },  // 3: CornerBR
        {w / 2 - h / 2, 0            },  // 4: EdgeTop
        {w / 2 - h / 2, ht - h       },  // 5: EdgeBottom
        {0,             ht / 2 - h / 2}, // 6: EdgeLeft
        {w - h,         ht / 2 - h / 2}, // 7: EdgeRight
    };

    for (int i = 0; i < 8; ++i) {
        m_handles[i]->setRect(QRectF(positions[i].x(), positions[i].y(), h, h));
        m_handles[i]->setBrush(handleBrush);
        m_handles[i]->setVisible(showHandles);
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
void CaptureWindow::showEvent(QShowEvent* event)
{
    QGraphicsView::showEvent(event);
#ifdef Q_OS_MACOS
    // The NSWindow handle is not valid until after the first paint cycle.
    // Defer the exclusion call so it runs after the window is fully attached.
    WId wid = winId();
    QTimer::singleShot(0, this, [wid]() {
        excludeWindowFromScreenCapture(reinterpret_cast<void*>(wid));
    });
#endif
}

void CaptureWindow::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    updateSceneGeometry();
    if (!m_suppressSignal)
        emit regionChanged(geometry());
}

void CaptureWindow::moveEvent(QMoveEvent* event)
{
    QGraphicsView::moveEvent(event);
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
