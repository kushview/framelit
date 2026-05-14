#include "capturewindow.hpp"

#include <QColor>
#include <QDebug>
#include <QFont>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QPen>
#include <QTimer>

#include "../platform/windowhelpers.hpp"
#ifdef Q_OS_WIN
#include <windows.h>
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

    // LCD readout items — bottom-left status, bottom-right dimensions.
    // Font is monospaced for that segment-display feel.
    QFont lcdFont;
    lcdFont.setPointSize(9);
    lcdFont.setFamily("Menlo");

    m_statusItem = m_scene->addSimpleText(QString());
    m_statusItem->setFont(lcdFont);
    m_statusItem->setZValue(2);

    m_dimsItem = m_scene->addSimpleText(QString());
    m_dimsItem->setFont(lcdFont);
    m_dimsItem->setZValue(2);

    setGeometry(0, 0, 800, 450);  // placeholder; AppController sets real position
    updateSceneGeometry();
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void CaptureWindow::onStateChanged(sc::AppState state)
{
    m_state = state;

    // Lock aspect ratio at the moment recording starts so that resizing
    // during recording produces a zoom effect rather than a crop change.
    if (state == AppState::Recording)
        m_lockedAspect = width() > 0 ? double(width()) / double(height()) : 0.0;
    else
        m_lockedAspect = 0.0;

    // Keep the frame click-through in every state so underlying apps remain
    // interactive. Movement is handled by CenterHandle.
    // WA_TransparentForMouseEvents alone is insufficient — it only affects
    // Qt's internal dispatch. Real click-through requires a platform call:
    //   macOS: NSWindow.ignoresMouseEvents
    //   Linux/X11: XShape input region set to empty
    const bool passthrough = true;
    setAttribute(Qt::WA_TransparentForMouseEvents, passthrough);
#ifdef Q_OS_MACOS
    setWindowClickThrough(reinterpret_cast<void*>(winId()), passthrough);
#endif
#ifdef Q_OS_LINUX
    sc::setWindowClickThrough(winId(), passthrough);
#endif
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if (passthrough)
        exStyle |= WS_EX_TRANSPARENT;
    else
        exStyle &= ~WS_EX_TRANSPARENT;
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);
#endif

    // Update border/handle colors and handle visibility — no repaint needed;
    // QGraphicsItem::setPen/setBrush triggers only the affected item's redraw.
    if (m_statusItem) {
        switch (state) {
        case AppState::Recording:  m_statusItem->setText("REC");  break;
        case AppState::Paused:     m_statusItem->setText("PAUSE"); break;
        case AppState::Processing: m_statusItem->setText("...");   break;
        default:                   m_statusItem->setText("READY"); break;
        }
    }
    updateSceneGeometry();
}

void CaptureWindow::onRegionChanged(const sc::CaptureRegion& region)
{
    // Suppress re-emission from resizeEvent/moveEvent so we don't loop
    // back through AppController when geometry is set programmatically.
    m_suppressSignal = true;
    setGeometry(region.rect);
    m_suppressSignal = false;

    if (m_dimsItem)
        m_dimsItem->setText(region.dimensionsString());
}

// ---------------------------------------------------------------------------
// Scene layout
// ---------------------------------------------------------------------------

void CaptureWindow::updateSceneGeometry()
{
    const qreal w  = width();
    const qreal ht = height();
    const qreal bw = kBorderWidth;
    const QColor c = borderColor();

    setSceneRect(0, 0, w, ht);

    m_borderItem->setRect(QRectF(bw / 2.0, bw / 2.0, w - bw, ht - bw));
    m_borderItem->setPen(QPen(c, bw));

    // Dim the readout color slightly from the border so it reads as secondary
    const QColor lcd = c.darker(115);
    const qreal margin = bw + 4;

    // Status — bottom-left
    m_statusItem->setBrush(lcd);
    const qreal sh = m_statusItem->boundingRect().height();
    m_statusItem->setPos(margin, ht - margin - sh);

    // Dimensions — bottom-right
    m_dimsItem->setBrush(lcd);
    const qreal dw = m_dimsItem->boundingRect().width();
    const qreal dh = m_dimsItem->boundingRect().height();
    m_dimsItem->setPos(w - margin - dw, ht - margin - dh);
}

// resizeEvent and moveEvent fire after macOS has committed the window
// geometry change to the compositor.
void CaptureWindow::showEvent(QShowEvent* event)
{
    QGraphicsView::showEvent(event);
    WId wid = winId();
    QTimer::singleShot(0, this, [wid]() {
        setupOverlayWindowOnShow(wid);
    });
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
    if (m_flashActive)             return QColor(0x22, 0xC5, 0x5E); // green flash
    switch (m_state) {
    case AppState::Recording:      return QColor(0xEF, 0x44, 0x44); // red
    case AppState::Paused:         return QColor(0xFA, 0xCC, 0x15); // yellow
    default:                       return QColor(0x94, 0xA3, 0xB8); // slate
    }
}

void CaptureWindow::flashGreen()
{
    m_flashActive = true;
    updateSceneGeometry();
    QTimer::singleShot(300, this, [this]() {
        m_flashActive = false;
        updateSceneGeometry();
    });
}

} // namespace sc
