#pragma once

#include "appcontroller.hpp"

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QVideoFrame>

namespace sc {

// Abstract base class for screen recording workers.
//
// Usage model:
//   auto* worker = new ConcreteRecorderWorker(region, settings);
//   auto* thread = new QThread;
//   worker->moveToThread(thread);
//   thread->start();
//   QObject::connect(thread, &QThread::started, worker, &RecorderWorker::start);
//
// All public slots are invoked on the worker thread via queued connections.
// The UI thread MUST NOT call start/stop/pause/resume directly — use signals.
//
// setCaptureRegion() is the one exception: it is thread-safe and may be
// called from any thread at any time, including mid-recording (LICEcap style).
class RecorderWorker : public QObject {
    Q_OBJECT

public:
    explicit RecorderWorker(const CaptureRegion& region,
                            const RecordingSettings& settings,
                            QObject* parent = nullptr);

    ~RecorderWorker() override;

    // Thread-safe. The UI thread calls this to move the capture boundary
    // while recording is in progress. Subclasses read via captureRegion().
    void setCaptureRegion(const CaptureRegion& region);

    // Thread-safe getter for the current capture region.
    // Public so AppController can snapshot the region at frame-emit time
    // without subclassing.
    CaptureRegion captureRegion() const;

public slots:
    virtual void start()  = 0;
    virtual void stop()   = 0;
    virtual void pause()  = 0;
    virtual void resume() = 0;

signals:
    // Emitted on every captured frame. Conversion to QImage happens on the
    // worker thread immediately so the underlying CMSampleBuffer (macOS) is
    // released back to the capture pool before this signal is queued — this
    // prevents ScreenCaptureKit backpressure from starving the pipeline.
    // CaptureRegion is snapshotted at emit time so a moving window is correct.
    void frameReady(QImage image, sc::CaptureRegion region);

    // Emitted approximately once per second with total elapsed recording time.
    void progressUpdated(qint64 elapsedMs);

    // Emitted once when capture stops (after stop() is processed).
    void recordingFinished();

    // Emitted when the capture backend reports an unrecoverable error
    // (e.g. macOS screen recording permission denied).
    void errorOccurred(const QString& message);

protected:
    RecordingSettings m_settings;

private:
    mutable QMutex m_regionMutex;
    CaptureRegion  m_region;
};

} // namespace sc
