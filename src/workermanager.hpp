#pragma once

#include "appcontroller.hpp" // CaptureRegion, RecordingSettings, OutputFormat

#include <QList>
#include <QObject>
#include <QString>
#include <qwindowdefs.h> // WId

class QThread;

namespace sc {

class RecorderWorker;
class RecordingStrategy;

// Owns the recording pipeline lifecycle: the capture worker (on its own
// QThread), the encode strategy, and the frame routing between them — including
// the disconnect-before-deleteLater dance that keeps queued frames from firing
// against a deleted strategy.
//
// This is the *mechanism* half of what used to live in AppController;
// AppController keeps the *policy* half (state transitions, UI). WorkerManager
// touches no windows and no state machine — it only reports lifecycle via
// signals, so it stays testable and single-concern.
class WorkerManager : public QObject {
    Q_OBJECT

public:
    explicit WorkerManager(QObject* parent = nullptr);
    ~WorkerManager() override;

    // Begin a recording: picks the concrete strategy from settings.format,
    // creates and starts the worker on its thread, and routes frames to the
    // strategy. Any previous session is torn down first.
    void start(const CaptureRegion& region,
               const RecordingSettings& settings,
               const QList<WId>& excludedWindowIds);

    void stop();
    void pause();
    void resume();

    // Update the capture boundary mid-recording (thread-safe; no-op if idle).
    void setCaptureRegion(const CaptureRegion& region);

    bool isActive() const { return m_worker != nullptr; }

signals:
    void progress(qint64 elapsedMs);         // ~1/s while recording
    void processingStarted();                // capture stopped; encoding begun
    void encodingProgress(float fraction);   // forwarded from the strategy
    void encodingFinished(const QString& filePath);
    void encodingFailed(const QString& reason);
    void captureError(const QString& message);

private slots:
    void onRecordingFinished(); // worker stopped → kick off strategy->finish()
    void onEncodingFinished(const QString& filePath);
    void onEncodingFailed(const QString& reason);

private:
    void attachWorker(RecorderWorker* worker);
    void teardownWorker();
    void teardownStrategy();

    QThread*           m_thread   = nullptr;
    RecorderWorker*    m_worker   = nullptr;
    RecordingStrategy* m_strategy = nullptr;
    // frameReady → strategy->onFrame; disconnected before the strategy is freed.
    QMetaObject::Connection m_frameConn;
};

} // namespace sc
