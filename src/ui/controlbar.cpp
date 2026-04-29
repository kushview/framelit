#include "controlbar.hpp"
#include "capturewindow.hpp"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QTimer>

#ifdef Q_OS_MACOS
#include "../platform/macos_window.h"
#endif

#define SC_USE_DEMO_MODE 0

namespace sc {

static constexpr int kBarHeight  = 36;
static constexpr int kBarMargin  = 4; // gap between capture rect and bar

ControlBar::ControlBar(CaptureWindow* captureWindow, QWidget* parent)
    : QWidget(parent)
    , m_captureWindow(captureWindow)
{
    setWindowFlags(Qt::FramelessWindowHint
                 | Qt::WindowStaysOnTopHint
                 | Qt::Tool);

    setFixedHeight(kBarHeight);

    // Dark background via stylesheet
    setStyleSheet("QWidget { background-color: #1e2029; color: #e2e8f0; }"
                  "QPushButton { padding: 2px 10px; border-radius: 3px; background-color: #334155; color: #e2e8f0; }"
                  "QPushButton:hover { background-color: #475569; }"
                  "QPushButton#recordBtn { background-color: #dc2626; }"
                  "QPushButton#recordBtn:hover { background-color: #ef4444; }");

    buildUi();

    // Poll the capture window geometry every 16ms (~60fps).
    // Simpler and more reliable than any signal/event timing approach.
    m_snapTimer = new QTimer(this);
    connect(m_snapTimer, &QTimer::timeout, this, [this]() {
        if (m_captureWindow)
            snapToRegion(m_captureWindow->geometry());
    });
    m_snapTimer->start(16);
}

bool ControlBar::demoMode() const
{
    return m_demoButton && m_demoButton->isChecked();
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void ControlBar::buildUi()
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(6);

    m_statusLabel = new QLabel("● Ready", this);
    m_statusLabel->setStyleSheet("color: #94a3b8; font-size: 12px;");
    layout->addWidget(m_statusLabel);

    layout->addSpacing(4);

    m_dimensionsLabel = new QLabel("800×450", this);
    m_dimensionsLabel->setStyleSheet("color: #cbd5e1; font-size: 12px; font-family: monospace;");
    layout->addWidget(m_dimensionsLabel);

    layout->addStretch();

    m_recordButton = new QPushButton("Record", this);
    m_recordButton->setObjectName("recordBtn");
    connect(m_recordButton, &QPushButton::clicked, this, &ControlBar::startRequested);
    layout->addWidget(m_recordButton);

    m_pauseButton = new QPushButton("Pause", this);
    m_pauseButton->setVisible(false);
    connect(m_pauseButton, &QPushButton::clicked, this, [this]() {
        if (m_state == AppState::Recording)
            emit pauseRequested();
        else if (m_state == AppState::Paused)
            emit resumeRequested();
    });
    layout->addWidget(m_pauseButton);

    m_stopButton = new QPushButton("Stop", this);
    m_stopButton->setVisible(false);
    connect(m_stopButton, &QPushButton::clicked, this, &ControlBar::stopRequested);
    layout->addWidget(m_stopButton);
#if SC_USE_DEMO_MODE
    m_demoButton = new QPushButton("Demo", this);
    m_demoButton->setCheckable(true);
    m_demoButton->setChecked(false);
    m_demoButton->setToolTip("Include capture frame in recording (for demos)");
    m_demoButton->setStyleSheet(
        "QPushButton { color: #94a3b8; border: 1px solid #334155; border-radius: 3px; padding: 2px 8px; }"
        "QPushButton:checked { color: #1e2029; background-color: #facc15; border-color: #facc15; font-weight: bold; }"
        "QPushButton:hover { border-color: #64748b; }");
    connect(m_demoButton, &QPushButton::toggled, this, [this](bool demo) {
        // Toggle NSWindowSharingNone so all screen recorders (QuickTime etc.) see/hide us.
        auto exclude = [](WId wid, bool ex) {
            setWindowCaptureExcluded(reinterpret_cast<void*>(wid), ex);
        };
        if (m_captureWindow) exclude(m_captureWindow->winId(), !demo);
        exclude(winId(), !demo);
    });
    layout->addWidget(m_demoButton);
#endif

    m_closeButton = new QPushButton("✕", this);
    m_closeButton->setFixedWidth(28);
    connect(m_closeButton, &QPushButton::clicked, qApp, &QApplication::quit);
    layout->addWidget(m_closeButton);

    // Resize grip — a small visual indicator at the right edge of the bar.
    // Hit zone is kGripSize px wide; cursor changes on hover.
    auto* grip = new QLabel("⊿", this);
    grip->setFixedWidth(kGripSize);
    grip->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grip->setStyleSheet(QString("color: #475569; font-size: %1px;").arg(kBarHeight - 10));
    grip->setCursor(Qt::SizeFDiagCursor);
    grip->setAttribute(Qt::WA_TransparentForMouseEvents); // bar handles the events
    layout->addWidget(grip);
    layout->setContentsMargins(8, 0, 0, 0); // remove right margin; grip provides it
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

void ControlBar::snapToRegion(const QRect& captureRect)
{
    // Try to place below; flip above if near the bottom of the screen.
    // Use captureRect.y() + captureRect.height() rather than bottom()
    // because QRect::bottom() == top() + height() - 1 in Qt.
    QScreen* screen = QApplication::primaryScreen();
    int screenBottom = screen ? screen->availableGeometry().bottom() : 9999;

    int barY = captureRect.y() + captureRect.height() + kBarMargin;
    if (barY + kBarHeight > screenBottom)
        barY = captureRect.y() - kBarMargin - kBarHeight;

    // Use move() + resize() separately — on macOS this produces more
    // reliable immediate native window repositioning than setGeometry()
    // when called from inside another window's mouse event handler.
    move(captureRect.x(), barY);
    resize(captureRect.width(), kBarHeight);
    repaint(); // flush immediately, don't wait for next display cycle
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void ControlBar::onStateChanged(sc::AppState state)
{
    m_state = state;
#ifdef Q_OS_MACOS
    // Mirror the capture window's level change: NSStatusWindowLevel (25) while
    // recording so the control bar can't be buried when another app activates.
    const int level = (state == AppState::Recording) ? 25 /*NSStatusWindowLevel*/
                                                      : 3  /*NSFloatingWindowLevel*/;
    setNSWindowLevel(reinterpret_cast<void*>(winId()), level);
#endifhello
    updateUiForState(state);
}

void ControlBar::onRegionChanged(const sc::CaptureRegion& region)
{
    m_dimensionsLabel->setText(region.dimensionsString());
    snapToRegion(region.rect);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void ControlBar::updateUiForState(AppState state)
{
    switch (state) {
    case AppState::Idle:
        m_statusLabel->setText("● Ready");
        m_statusLabel->setStyleSheet("color: #94a3b8; font-size: 12px;");
        m_recordButton->setVisible(true);
        m_pauseButton->setVisible(false);
        m_stopButton->setVisible(false);
        break;

    case AppState::Recording:
        m_statusLabel->setText("⏺ Recording");
        m_statusLabel->setStyleSheet("color: #ef4444; font-size: 12px;");
        m_recordButton->setVisible(false);
        m_pauseButton->setText("Pause");
        m_pauseButton->setVisible(true);
        m_stopButton->setVisible(true);
        break;

    case AppState::Paused:
        m_statusLabel->setText("⏸ Paused");
        m_statusLabel->setStyleSheet("color: #facc15; font-size: 12px;");
        m_pauseButton->setText("Resume");
        break;

    case AppState::Processing:
        m_statusLabel->setText("⏳ Processing…");
        m_statusLabel->setStyleSheet("color: #94a3b8; font-size: 12px;");
        m_recordButton->setVisible(false);
        m_pauseButton->setVisible(false);
        m_stopButton->setVisible(false);
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Drag the whole apparatus by clicking the bar background;
// resize the capture window by dragging the bottom-right grip zone.
// ---------------------------------------------------------------------------

bool ControlBar::isInGripZone(const QPoint& localPos) const
{
    return localPos.x() >= width() - kGripSize;
}

void ControlBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_captureWindow) {
        if (isInGripZone(event->pos())) {
            m_resizing          = true;
            m_dragStart         = event->globalPosition().toPoint();
            m_captureRectAtPress = m_captureWindow->geometry();
        } else {
            m_dragging      = true;
            m_dragStart     = event->globalPosition().toPoint();
            m_captureOrigin = m_captureWindow->pos();
        }
    }
    QWidget::mousePressEvent(event);
}

void ControlBar::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_captureWindow) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (m_resizing) {
        QPoint delta = event->globalPosition().toPoint() - m_dragStart;
        QRect r = m_captureRectAtPress;

        int newW = qMax(CaptureWindow::kMinDimension, r.width()  + delta.x());
        int newH = qMax(CaptureWindow::kMinDimension, r.height() + delta.y());

        // If the capture window has an aspect lock (recording), derive height
        // from width so the zoom stays proportional.
        double aspect = m_captureWindow->lockedAspect();
        if (aspect > 0.0)
            newH = qMax(CaptureWindow::kMinDimension, int(newW / aspect));

        r.setWidth(newW);
        r.setHeight(newH);
        m_captureWindow->setGeometry(r);
    } else if (m_dragging) {
        QPoint delta = event->globalPosition().toPoint() - m_dragStart;
        m_captureWindow->move(m_captureOrigin + delta);
    } else {
        // Cursor feedback on hover
        setCursor(isInGripZone(event->pos()) ? Qt::SizeFDiagCursor
                                             : Qt::ArrowCursor);
    }
    QWidget::mouseMoveEvent(event);
}

void ControlBar::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging  = false;
        m_resizing  = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void ControlBar::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
#ifdef Q_OS_MACOS
    WId wid = winId();
    QTimer::singleShot(0, this, [wid]() {
        excludeWindowFromScreenCapture(reinterpret_cast<void*>(wid));
        setWindowHidesOnDeactivate(reinterpret_cast<void*>(wid), false);
    });
#endif
}

} // namespace sc
