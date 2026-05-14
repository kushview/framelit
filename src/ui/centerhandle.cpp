#include "centerhandle.hpp"

#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

#include "../platform/windowhelpers.hpp"
#ifdef Q_OS_MACOS
#include "../platform/macos_window.h"
#endif

namespace sc {

CenterHandle::CenterHandle(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint
                 | Qt::WindowStaysOnTopHint
                 | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setFixedSize(kSize, kSize);
    setCursor(Qt::SizeAllCursor);
    setMouseTracking(true);

    m_wheelTimer.setSingleShot(true);
    connect(&m_wheelTimer, &QTimer::timeout, this, &CenterHandle::flushWheelResize);
}

void CenterHandle::onRegionChanged(const sc::CaptureRegion& region)
{
    const QRect r = region.rect;
    move(r.center().x() - width() / 2,
         r.center().y() - height() / 2);
}

void CenterHandle::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    WId wid = winId();
    QTimer::singleShot(0, this, [wid]() {
        setupOverlayWindowOnShow(wid);
#ifdef Q_OS_MACOS
        setNSWindowLevel(reinterpret_cast<void*>(wid), kStatusWindowLevel);
#endif
    });
}

void CenterHandle::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false); // crisp pixel-aligned lines

    const int w = width();
    const int h = height();
    const QPoint c = rect().center();

    // --- opacity levels ---
    const int fillAlpha   = m_hovered ? 90  : 20;
    const int borderAlpha = m_hovered ? 230 : 70;
    const int glyphAlpha  = m_hovered ? 255 : 90;

    // Rounded-rect body — dark HUD fill
    const QRectF body(0.5, 0.5, w - 1.0, h - 1.0);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 8, 20, fillAlpha));
    p.drawRoundedRect(body, 4, 4);

    // Thin 1px border — cyan/slate tint
    const QColor borderColor = m_hovered
        ? QColor(100, 220, 255, borderAlpha)
        : QColor(148, 163, 184, borderAlpha);
    p.setPen(QPen(borderColor, 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(body, 4, 4);

    // Corner tick marks (LCD-style registration marks)
    const QColor tickColor(borderColor.red(), borderColor.green(), borderColor.blue(), borderAlpha / 2);
    p.setPen(QPen(tickColor, 1.0));
    const int tk = 5; // tick length
    const int m  = 4; // margin from corner
    // top-left
    p.drawLine(m, m, m + tk, m);
    p.drawLine(m, m, m, m + tk);
    // top-right
    p.drawLine(w - m, m, w - m - tk, m);
    p.drawLine(w - m, m, w - m, m + tk);
    // bottom-left
    p.drawLine(m, h - m, m + tk, h - m);
    p.drawLine(m, h - m, m, h - m - tk);
    // bottom-right
    p.drawLine(w - m, h - m, w - m - tk, h - m);
    p.drawLine(w - m, h - m, w - m, h - m - tk);

    // Move arrows — four-directional, LCD segmented style
    const QColor glyphColor = m_hovered
        ? QColor(120, 230, 255, glyphAlpha)
        : QColor(200, 210, 220, glyphAlpha);
    p.setPen(QPen(glyphColor, 1.0));

    const int arm  = 7; // arrow arm length from center
    const int head = 3; // arrowhead size

    // horizontal arms
    p.drawLine(c.x() - 2, c.y(), c.x() - arm, c.y());
    p.drawLine(c.x() + 2, c.y(), c.x() + arm, c.y());
    // vertical arms
    p.drawLine(c.x(), c.y() - 2, c.x(), c.y() - arm);
    p.drawLine(c.x(), c.y() + 2, c.x(), c.y() + arm);

    // arrowheads (three-line chevron each direction)
    // left
    p.drawLine(c.x() - arm, c.y(), c.x() - arm + head, c.y() - head);
    p.drawLine(c.x() - arm, c.y(), c.x() - arm + head, c.y() + head);
    // right
    p.drawLine(c.x() + arm, c.y(), c.x() + arm - head, c.y() - head);
    p.drawLine(c.x() + arm, c.y(), c.x() + arm - head, c.y() + head);
    // up
    p.drawLine(c.x(), c.y() - arm, c.x() - head, c.y() - arm + head);
    p.drawLine(c.x(), c.y() - arm, c.x() + head, c.y() - arm + head);
    // down
    p.drawLine(c.x(), c.y() + arm, c.x() - head, c.y() + arm - head);
    p.drawLine(c.x(), c.y() + arm, c.x() + head, c.y() + arm - head);
}

void CenterHandle::enterEvent(QEnterEvent* event)
{
    Q_UNUSED(event)
    m_hovered = true;
    update();
}

void CenterHandle::leaveEvent(QEvent* event)
{
    Q_UNUSED(event)
    if (!m_dragging) {
        m_hovered = false;
        update();
    }
}

void CenterHandle::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_dragging = true;
    m_lastGlobal = event->globalPosition().toPoint();
    event->accept();
}

void CenterHandle::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging)
        return;

    const QPoint current = event->globalPosition().toPoint();
    const QPoint delta = current - m_lastGlobal;
    if (!delta.isNull())
        emit dragDelta(delta);
    m_lastGlobal = current;
    event->accept();
}

void CenterHandle::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_dragging = false;
    // If cursor left the widget during drag, clear highlight now
    if (!rect().contains(event->position().toPoint())) {
        m_hovered = false;
        update();
    }
    event->accept();
}

void CenterHandle::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    emit screenshotRequested();
    event->accept();
}

void CenterHandle::wheelEvent(QWheelEvent* event)
{
    const QPoint delta = event->angleDelta();
    const int primaryDelta = qAbs(delta.y()) >= qAbs(delta.x()) ? delta.y() : delta.x();
    if (primaryDelta == 0)
        return;

    m_wheelAccumulator += primaryDelta;
    flushWheelResize();
    event->accept();
}

void CenterHandle::flushWheelResize()
{
    if (m_wheelTimer.isActive())
        return;
    if (qAbs(m_wheelAccumulator) < kWheelStepDelta)
        return;

    const int accumulatorSign = m_wheelAccumulator > 0 ? +1 : -1;
    const int direction = -accumulatorSign;
    m_wheelAccumulator -= accumulatorSign * kWheelStepDelta;
    emit wheelResizeRequested(direction);
    m_wheelTimer.start(kWheelThrottleMs);
}

} // namespace sc
