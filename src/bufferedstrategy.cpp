#include "bufferedstrategy.hpp"
#include "encoding/gifencoder.hpp"
#include "outputpath.hpp"

#include <QDebug>
#include <QThread>

namespace sc {

// Watchdog budget: a buffered GIF encode is CPU-bound and local, so anything
// past this almost certainly means the encoder wedged. Fail loudly instead of
// leaving the UI stuck on "Processing…".
static constexpr int kEncodeTimeoutMs = 120'000; // 2 minutes

BufferedStrategy::BufferedStrategy(const RecordingSettings& settings,
                                   QObject* parent)
    : RecordingStrategy(settings, parent)
    , m_frameStore(new FrameStore(this))
{
    // Bounds RAM: excess frames are dropped and the recording truncates.
    connect(m_frameStore, &FrameStore::bufferLimitReached, this, []() {
        qWarning("[BufferedStrategy] frame buffer byte budget reached — "
                 "recording truncated to bound memory.");
    });
}

BufferedStrategy::~BufferedStrategy()
{
    stopEncoderThread();
}

void BufferedStrategy::stopEncoderThread()
{
    if (m_watchdog) {
        m_watchdog->stop();
        m_watchdog->deleteLater();
        m_watchdog = nullptr;
    }
    if (m_encoderThread) {
        m_encoderThread->quit();
        m_encoderThread->wait();
        m_encoderThread->deleteLater();
        m_encoderThread = nullptr;
    }
}

void BufferedStrategy::onFrame(const QImage& image, const CaptureRegion& region)
{
    m_frameStore->addFrame(image, region);
}

void BufferedStrategy::finish()
{
    const int count = m_frameStore->frameCount();
    qDebug("BufferedStrategy::finish(): %d frames", count);

    if (count == 0) {
        emit encodingFailed(QStringLiteral("No frames captured."));
        return;
    }

    const QString outputPath = makeCaptureOutputPath(
        m_settings.outputDir,
        QStringLiteral("gif"));

    int sizeSourceIndex = count - 1;
    while (sizeSourceIndex >= 0 && m_frameStore->frameAt(sizeSourceIndex).image.isNull())
        --sizeSourceIndex;
    if (sizeSourceIndex < 0) {
        emit encodingFailed(QStringLiteral("No decodable frames captured."));
        return;
    }

    const TaggedFrame& sizeFrame = m_frameStore->frameAt(sizeSourceIndex);

    GifExportSettings gifSettings;
    gifSettings.outputFps  = qMin(10, m_settings.fps);
    gifSettings.useCurrentFrameSize = m_settings.gifUseFrameSize;
    gifSettings.outputSize = m_settings.gifUseFrameSize
        ? sizeFrame.region.rect.size()
        : m_settings.gifOutputSize;
    if (m_settings.hiDpi) {
        const QScreen* screen = sizeFrame.region.screen;
        const qreal dpr = screen ? screen->devicePixelRatio() : 2.0;
        gifSettings.outputSize *= dpr;
    }
    gifSettings.quality    = m_settings.gifQuality;
    gifSettings.letterbox  = m_settings.letterbox;

    // Tear down any leftover encoder thread (shouldn't happen, defensive).
    stopEncoderThread();

    m_encoderThread = new QThread(this);
    auto* encoder = new GifEncoder(m_frameStore, gifSettings, m_settings.fps, outputPath);
    encoder->moveToThread(m_encoderThread);

    connect(m_encoderThread, &QThread::started,
            encoder, &GifEncoder::encode);
    connect(encoder, &GifEncoder::progress,
            this, &BufferedStrategy::encodingProgress);
    connect(encoder, &GifEncoder::finished,
            this, &BufferedStrategy::encodingFinished);
    connect(encoder, &GifEncoder::failed,
            this, &BufferedStrategy::encodingFailed);
    connect(m_encoderThread, &QThread::finished,
            encoder, &QObject::deleteLater);
    // Once encoding finishes (success or fail), clean up the thread via the
    // single shared teardown path (also used by the destructor and watchdog).
    connect(encoder, &GifEncoder::finished, this,
            [this](const QString&) { stopEncoderThread(); });
    connect(encoder, &GifEncoder::failed, this,
            [this](const QString&) { stopEncoderThread(); });

    // Watchdog: if the encoder never emits finished/failed, fail the encode so
    // the UI doesn't hang on "Processing…" forever.
    m_watchdog = new QTimer(this);
    m_watchdog->setSingleShot(true);
    connect(m_watchdog, &QTimer::timeout, this, [this]() {
        qWarning("[BufferedStrategy] encode timed out after %d ms — aborting.",
                 kEncodeTimeoutMs);
        // Bounded teardown so a wedged encoder can never freeze the main thread.
        if (m_encoderThread) {
            m_encoderThread->quit();
            if (!m_encoderThread->wait(2000)) {
                m_encoderThread->terminate(); // last resort for a hung encode
                m_encoderThread->wait(1000);
            }
            m_encoderThread->deleteLater();
            m_encoderThread = nullptr;
        }
        m_watchdog->deleteLater();
        m_watchdog = nullptr;
        emit encodingFailed(QStringLiteral("Encoding timed out."));
    });
    m_watchdog->start(kEncodeTimeoutMs);

    m_encoderThread->start();
}

} // namespace sc
