#pragma once

#include "../recorderworker.hpp"

#include <QElapsedTimer>

class QMediaCaptureSession;
class QScreenCapture;
class QTimer;
class QVideoFrame;
class QVideoSink;

namespace sc {

// Concrete RecorderWorker that uses QScreenCapture + QMediaCaptureSession +
// QVideoSink to capture the screen (Qt 6.5+).
//
// Threading: lives on a dedicated QThread (managed by AppController).
// All QScreenCapture / QMediaCaptureSession / QVideoSink objects are created
// in start() on that thread. videoFrameChanged signals from QVideoSink arrive
// on Qt multimedia's internal thread; Qt::AutoConnection routes them to this
// object's thread via the event loop.
//
// Coordinate mapping: QScreenCapture delivers full-screen frames in physical
// pixels. CaptureRegion::rect is in logical (device-independent) pixels.
// onFrameReceived() maps the rect to physical pixels using QScreen::devicePixelRatio()
// before calling QImage::copy().
class ScreenCaptureWorker : public RecorderWorker {
    Q_OBJECT

public:
    explicit ScreenCaptureWorker(const CaptureRegion& region,
                                 const RecordingSettings& settings,
                                 QObject* parent = nullptr);
    ~ScreenCaptureWorker() override;

public slots:
    void start()  override;
    void stop()   override;
    void pause()  override;
    void resume() override;

private slots:
    void onFrameReceived(const QVideoFrame& frame);
    void onCaptureError(int error, const QString& message);

private:
    QScreenCapture*       m_capture       = nullptr;
    QMediaCaptureSession* m_session       = nullptr;
    QVideoSink*           m_sink          = nullptr;
    QTimer*               m_progressTimer = nullptr;

    QElapsedTimer m_elapsed;
    qint64        m_lastFrameMs = -1;  // ms timestamp of last emitted frame
    qint64        m_frameIntervalMs;   // 1000 / settings.fps

    bool m_running = false;
    bool m_paused  = false;
};

} // namespace sc
