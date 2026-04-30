#include "streamingstrategy.hpp"
#include "encoding/videoencoder.hpp"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>

namespace sc {

// Crop a full-screen frame to the logical capture region.
// The backend may deliver frames at logical (1x) or physical (dpr x) resolution,
// so we measure the actual scale from frame dims vs. the screen's logical geometry.
static QImage cropToRegion(const QImage& image, const CaptureRegion& region)
{
    const QRect screenLogical = region.screen
        ? region.screen->geometry()
        : QRect(0, 0, image.width(), image.height());

    const qreal scaleX = screenLogical.width()  > 0
        ? (qreal)image.width()  / screenLogical.width()  : 1.0;
    const qreal scaleY = screenLogical.height() > 0
        ? (qreal)image.height() / screenLogical.height() : 1.0;

    QRect localLogical = region.rect.translated(-screenLogical.topLeft());
    QRect scaled(
        qRound(localLogical.x()      * scaleX),
        qRound(localLogical.y()      * scaleY),
        qRound(localLogical.width()  * scaleX),
        qRound(localLogical.height() * scaleY)
    );
    scaled = scaled.intersected(QRect(0, 0, image.width(), image.height()));
    if (scaled.isEmpty())
        return image;
    return image.copy(scaled);
}

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
    const QImage image = cropToRegion(rawImage, region);

    if (!m_started) {
        const QString timestamp =
            QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd-HHmmss"));
        const QString outputDir = m_settings.outputDir.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
            : m_settings.outputDir;
        QDir().mkpath(outputDir);

        const QString ext = (m_settings.format == OutputFormat::WebM) ? "webm" : "mp4";
        m_outputPath = outputDir + QDir::separator() +
                       QStringLiteral("capture-%1.%2").arg(timestamp, ext);

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
