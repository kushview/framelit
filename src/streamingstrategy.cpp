#include "streamingstrategy.hpp"
#include "capture/cropgeometry.hpp"
#include "encoding/videoencoder.hpp"
#include "imageutil.hpp"
#include "outputpath.hpp"

#include <QDebug>

namespace sc {

StreamingStrategy::StreamingStrategy(const RecordingSettings& settings, QObject* parent)
    : RecordingStrategy(settings, parent)
    , m_encoder(new VideoEncoder(settings, this))
{
    connect(m_encoder, &VideoEncoder::encodingFinished,
            this,      &StreamingStrategy::encodingFinished);
    connect(m_encoder, &VideoEncoder::encodingFailed,
            this,      &StreamingStrategy::encodingFailed);
}

void StreamingStrategy::onFrame(const QImage& rawImage, const CaptureRegion& region)
{
    QImage image = rawImage.copy(physicalCropRect(region, rawImage.size()));

    // Fit into output size while preserving aspect ratio, then center on a
    // black canvas to avoid stretching when the capture aspect differs.
    // If HiDPI is enabled, multiply base size by the screen's actual device
    // pixel ratio rather than assuming 2×.
    QSize outputSize = m_settings.outputSize;
    if (m_settings.hiDpi && region.screen)
        outputSize *= region.screen->devicePixelRatio();
    if (!image.isNull() && image.size() != outputSize)
        image = scaleImage(image, outputSize, m_settings.letterbox);

    if (!m_started) {
        const QString ext = (m_settings.format == OutputFormat::WebM) ? "webm" : "mp4";
        m_outputPath = makeCaptureOutputPath(m_settings.outputDir, ext);

        if (!m_encoder->start(m_outputPath, image.size())) {
            emit encodingFailed(QStringLiteral("Failed to start video recorder."));
            return;
        }
        m_started = true;
        qDebug("[StreamingStrategy] recording to %s  frame size: %dx%d",
               qPrintable(m_outputPath), image.width(), image.height());
    }

    m_encoder->sendFrame(image);
}

void StreamingStrategy::finish()
{
    if (!m_started) {
        emit encodingFailed(QStringLiteral("No frames captured."));
        return;
    }
    m_encoder->stop();
}

} // namespace sc
