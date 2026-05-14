#include "qtscreenbackend.hpp"

#include <QMediaCaptureSession>
#include <QScreen>
#include <QScreenCapture>
#include <QVideoFrame>
#include <QVideoSink>

namespace sc {

QtScreenCaptureBackend::QtScreenCaptureBackend(int fps, QObject* parent)
    : ScreenCaptureBackend(fps, parent)
{}

QtScreenCaptureBackend::~QtScreenCaptureBackend()
{
    if (m_running)
        stopCapture();
}

void QtScreenCaptureBackend::setScreen(QScreen* screen)
{
    m_screen = screen;
}

void QtScreenCaptureBackend::setExcludedWindowIds(const QList<WId>&)
{
    // No-op: on Windows, SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)
    // is applied to overlay windows at creation time and is respected by the
    // OS compositor, so the Qt capture pipeline automatically omits them.
}

void QtScreenCaptureBackend::startCapture()
{
    m_running = true;
    m_errorReported = false;

    m_capture = new QScreenCapture(this);
    m_capture->setScreen(m_screen);

    m_session = new QMediaCaptureSession(this);
    m_session->setScreenCapture(m_capture);

    m_sink = new QVideoSink(this);
    m_session->setVideoSink(m_sink);

    connect(m_sink, &QVideoSink::videoFrameChanged,
            this,   &QtScreenCaptureBackend::onVideoFrameChanged,
            Qt::AutoConnection);

    connect(m_capture, &QScreenCapture::errorOccurred,
            this, [this](QScreenCapture::Error code, const QString& msg) {
                onCaptureError(static_cast<int>(code), msg);
            },
            Qt::QueuedConnection);

    m_capture->setActive(true);
}

void QtScreenCaptureBackend::stopCapture()
{
    m_running = false;

    if (m_sink)
        disconnect(m_sink, &QVideoSink::videoFrameChanged, this, nullptr);
    if (m_capture)
        m_capture->setActive(false);

    // Delete in reverse construction order on this (worker) thread.
    // QMediaCaptureSession's destructor does a blocking main-thread call;
    // deleting here avoids the deadlock that would occur during thread cleanup.
    delete m_session; m_session = nullptr;
    delete m_capture; m_capture = nullptr;
    delete m_sink;    m_sink    = nullptr;
}

void QtScreenCaptureBackend::onVideoFrameChanged(const QVideoFrame& videoFrame)
{
    if (!m_running) return;

    QImage img = videoFrame.toImage();
    if (img.isNull())
        return;

    // On Windows, QScreenCapture naturally captures the system cursor
    // as rendered by the OS compositor. No manual compositing is needed.

    emit frameArrived(img);
}

void QtScreenCaptureBackend::onCaptureError(int /*code*/, const QString& message)
{
    if (m_errorReported) return;
    m_errorReported = true;
    emit errorOccurred(message);
}

} // namespace sc
