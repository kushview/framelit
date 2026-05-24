#include "bufferedstrategy.hpp"
#include "encoding/gifencoder.hpp"
#include "outputpath.hpp"

#include <QDebug>
#include <QThread>

namespace sc {

BufferedStrategy::BufferedStrategy(const RecordingSettings& settings,
                                   QObject* parent)
    : RecordingStrategy(settings, parent)
    , m_frameStore(new FrameStore(this))
{}

BufferedStrategy::~BufferedStrategy()
{
    if (m_encoderThread) {
        m_encoderThread->quit();
        m_encoderThread->wait();
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
    gifSettings.quality    = m_settings.quality;
    gifSettings.letterbox  = m_settings.letterbox;

    // Tear down any leftover encoder thread (shouldn't happen, defensive).
    if (m_encoderThread) {
        m_encoderThread->quit();
        m_encoderThread->wait();
        m_encoderThread->deleteLater();
        m_encoderThread = nullptr;
    }

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
    // Once encoding finishes (success or fail), clean up the thread.
    connect(encoder, &GifEncoder::finished, this, [this](const QString&) {
        if (m_encoderThread) {
            m_encoderThread->quit();
            m_encoderThread->wait();
            m_encoderThread->deleteLater();
            m_encoderThread = nullptr;
        }
    });
    connect(encoder, &GifEncoder::failed, this, [this](const QString&) {
        if (m_encoderThread) {
            m_encoderThread->quit();
            m_encoderThread->wait();
            m_encoderThread->deleteLater();
            m_encoderThread = nullptr;
        }
    });

    m_encoderThread->start();
}

} // namespace sc
