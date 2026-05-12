#include "closebutton.hpp"

#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

#ifdef Q_OS_MACOS
#include "../platform/macos_window.h"
#endif

namespace sc {

static constexpr int kStatusWindowLevel = 25; // NSStatusWindowLevel

CloseButton::CloseButton(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint
                 | Qt::WindowStaysOnTopHint
                 | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setFixedSize(kSize, kSize);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
}

void CloseButton::onRegionChanged(const sc::CaptureRegion& region)
{
    const QRect r = region.rect;
    // Position at top-right corner of capture region, with small margin
    const int margin = 4;
    move(r.right() - width() - margin,
         r.top() + margin);
}

void CloseButton::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
#ifdef Q_OS_MACOS
    WId wid = winId();
    QTimer::singleShot(0, this, [wid]() {
        setWindowHidesOnDeactivate(reinterpret_cast<void*>(wid), false);
        setNSWindowLevel(reinterpret_cast<void*>(wid), kStatusWindowLevel);
        excludeWindowFromScreenCapture(reinterpret_cast<void*>(wid));
    });
#endif
}

void CloseButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int w = width();
    const int h = height();

    // --- opacity levels ---
    const int fillAlpha   = m_pressed ? 120 : (m_hovered ? 90 : 20);
    const int borderAlpha = m_pressed ? 255 : (m_hovered ? 230 : 70);
    const int glyphAlpha  = m_pressed ? 255 : (m_hovered ? 255 : 90);

    // Rounded-rect body — dark HUD fill
    const QRectF body(0.5, 0.5, w - 1.0, h - 1.0);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 8, 20, fillAlpha));
    p.drawRoundedRect(body, 4, 4);

    // Thin 1px border — red/crimson tint for close button
    const QColor borderColor = m_pressed
        ? QColor(255, 100, 100, borderAlpha)
        : (m_hovered ? QColor(255, 120, 120, borderAlpha) : QColor(200, 100, 100, borderAlpha));
    p.setPen(QPen(borderColor, 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(body, 4, 4);

    // X glyph
    const QColor glyphColor = m_pressed
        ? QColor(255, 200, 200, glyphAlpha)
        : (m_hovered ? QColor(255, 150, 150, glyphAlpha) : QColor(220, 150, 150, glyphAlpha));
    p.setPen(QPen(glyphColor, 1.8));

    const int margin = 6;
    p.drawLine(margin, margin, w - margin, h - margin);
    p.drawLine(w - margin, margin, margin, h - margin);
}

void CloseButton::enterEvent(QEnterEvent* event)
{
    Q_UNUSED(event)
    m_hovered = true;
    update();
}

void CloseButton::leaveEvent(QEvent* event)
{
    Q_UNUSED(event)
    if (!m_pressed) {
        m_hovered = false;
        update();
    }
}

void CloseButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_pressed = true;
    update();
    event->accept();
}

void CloseButton::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    
    const bool wasPressed = m_pressed;
    m_pressed = false;
    
    // Only emit closeRequested if we're still inside the button
    if (wasPressed && rect().contains(event->position().toPoint())) {
        emit closeRequested();
    }
    
    // If cursor left the widget during press, clear highlight
    if (!rect().contains(event->position().toPoint())) {
        m_hovered = false;
    }
    
    update();
    event->accept();
}

} // namespace sc
