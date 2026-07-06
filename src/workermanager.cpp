#include "workermanager.hpp"

#include "bufferedstrategy.hpp"
#include "capture/screencaptureworker.hpp"
#include "recorderworker.hpp"
#include "recordingstrategy.hpp"
#include "streamingstrategy.hpp"

#include <QThread>

namespace sc {

WorkerManager::WorkerManager(QObject* parent)
    : QObject(parent)
{}

WorkerManager::~WorkerManager()
{
    teardownWorker();
    teardownStrategy();
}

void WorkerManager::start(const CaptureRegion& region,
                          const RecordingSettings& settings,
                          const QList<WId>& excludedWindowIds)
{
    // Create the strategy before the worker so it's ready for the first frame.
    if (settings.format == OutputFormat::Gif)
        m_strategy = new BufferedStrategy(settings, this);
    else
        m_strategy = new StreamingStrategy(settings, this);

    connect(m_strategy, &RecordingStrategy::encodingProgress,
            this, &WorkerManager::encodingProgress);
    connect(m_strategy, &RecordingStrategy::encodingFinished,
            this, &WorkerManager::onEncodingFinished);
    connect(m_strategy, &RecordingStrategy::encodingFailed,
            this, &WorkerManager::onEncodingFailed);

    auto* worker = new ScreenCaptureWorker(region, settings, excludedWindowIds);
    attachWorker(worker);

    // Route captured frames to the strategy. Store the connection so it can be
    // disconnected before the strategy is torn down — queued frames must not
    // fire against a deleted object.
    m_frameConn = connect(worker, &RecorderWorker::frameReady,
            this, [this](const QImage& image, const CaptureRegion& frameRegion) {
                if (m_strategy)
                    m_strategy->onFrame(image, frameRegion);
            },
            Qt::QueuedConnection);

    connect(worker, &RecorderWorker::errorOccurred,
            this, &WorkerManager::captureError,
            Qt::QueuedConnection);

    QMetaObject::invokeMethod(m_worker, "start", Qt::QueuedConnection);
}

void WorkerManager::stop()
{
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);
}

void WorkerManager::pause()
{
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "pause", Qt::QueuedConnection);
}

void WorkerManager::resume()
{
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "resume", Qt::QueuedConnection);
}

void WorkerManager::setCaptureRegion(const CaptureRegion& region)
{
    if (m_worker)
        m_worker->setCaptureRegion(region);
}

void WorkerManager::onRecordingFinished()
{
    // start() always creates a strategy, and encoding only completes after the
    // worker stops, so m_strategy is non-null here. Guard defensively anyway.
    if (!m_strategy)
        return;
    emit processingStarted();
    m_strategy->finish();
}

void WorkerManager::onEncodingFinished(const QString& filePath)
{
    teardownStrategy();
    emit encodingFinished(filePath);
}

void WorkerManager::onEncodingFailed(const QString& reason)
{
    teardownStrategy();
    emit encodingFailed(reason);
}

void WorkerManager::attachWorker(RecorderWorker* worker)
{
    teardownWorker();

    m_worker = worker;
    m_thread = new QThread(this);
    m_worker->moveToThread(m_thread);

    connect(m_worker, &RecorderWorker::recordingFinished,
            this, &WorkerManager::onRecordingFinished,
            Qt::QueuedConnection);
    connect(m_worker, &RecorderWorker::progressUpdated,
            this, &WorkerManager::progress,
            Qt::QueuedConnection);

    // Clean up the thread when the worker is done, and null our pointers
    // immediately so they're never left dangling.
    connect(m_thread, &QThread::finished, this, [this]() {
        m_worker = nullptr;
        m_thread = nullptr;
    }, Qt::QueuedConnection);
    connect(m_worker, &RecorderWorker::recordingFinished,
            m_thread, &QThread::quit,
            Qt::QueuedConnection);
    connect(m_thread, &QThread::finished,
            m_worker, &QObject::deleteLater);

    m_thread->start();
}

void WorkerManager::teardownWorker()
{
    if (!m_thread)
        return;
    m_thread->quit();
    m_thread->wait();
    // m_worker is deleted by the deleteLater connection above.
    m_worker = nullptr;
    m_thread = nullptr;
}

void WorkerManager::teardownStrategy()
{
    // Disconnect frame delivery first — queued frames must not fire against a
    // strategy that has been marked for deletion.
    disconnect(m_frameConn);
    if (m_strategy) {
        m_strategy->deleteLater();
        m_strategy = nullptr;
    }
}

} // namespace sc
