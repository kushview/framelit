#pragma once

#include "../recorderworker.hpp"
#include "../platform/screencapturebackend.hpp"

#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <qwindowdefs.h>  // WId

class QTimer;

namespace sc {

// Concrete RecorderWorker that routes screen capture through a
// ScreenCaptureBackend (platform-specific).
//
// On macOS the SCK backend is used: it honours setExcludedWindowIds() so
// overlay windows (CaptureWindow, ControlBar) are absent from captured frames.
// On Windows/Linux the Qt backend is used: QScreenCapture + QVideoSink.
//
// Threading: lives on a dedicated QThread managed by AppController.
// The backend is created in the constructor (main thread) and moved to the
// worker thread along with this object via Qt's parent–child moveToThread.
// start() / stop() are invoked via QMetaObject::invokeMethod on the worker thread.
class ScreenCaptureWorker : public RecorderWorker {
    Q_OBJECT

public:
    explicit ScreenCaptureWorker(const CaptureRegion& region,
                                 const RecordingSettings& settings,
                                 const QList<WId>& excludedWindowIds = {},
                                 QObject* parent = nullptr);
    ~ScreenCaptureWorker() override;

public slots:
    void start()  override;
    void stop()   override;
    void pause()  override;
    void resume() override;

private slots:
    void onFrameArrived(const QImage& image);

private:
    ScreenCaptureBackend* m_backend        = nullptr;
    QTimer*               m_progressTimer  = nullptr;

    QElapsedTimer m_elapsed;
    qint64        m_lastFrameMs    = -1;
    qint64        m_frameIntervalMs;

    int  m_framesReceived = 0;
    int  m_framesKept     = 0;

    bool m_running = false;
    bool m_paused  = false;
};

} // namespace sc

