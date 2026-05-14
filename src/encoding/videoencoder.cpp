#include "videoencoder.hpp"
#include "audioengine.hpp"

#include <QDateTime>
#include <QDebug>
#include <QMediaDevices>
#include <QMediaFormat>
#include <QUrl>
#include <QVideoFrame>

namespace sc {

VideoEncoder::VideoEncoder(const RecordingSettings& settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_session.setVideoFrameInput(&m_input);

    // Audio: add the default microphone to the session when requested.
    // QMediaRecorder will mux it automatically alongside the video frames.
    // The default QAudioInput() selects the system default input device.
    // Sync note: audio uses the system audio clock; video is stamped via
    // QElapsedTimer. Both start at roughly record-start, which is close
    // enough for short clips. Plan Milestone 8 will add proper PTS alignment.
    if (m_settings.captureAudio) {
        // Resolve device by ID if one was selected; fall back to system default.
        if (!m_settings.audioDeviceId.isEmpty()) {
            const QAudioDevice dev = audio::resolveInputDevice(m_settings.audioDeviceId);
            if (!dev.isNull()) {
                m_audioInput.setDevice(dev);
                qDebug("[VideoEncoder] audio device: %s",
                       qPrintable(dev.description()));
            } else {
                qDebug("[VideoEncoder] audio device not found, using system default");
            }
        } else {
            qDebug("[VideoEncoder] audio device: system default");
        }
        m_session.setAudioInput(&m_audioInput);
    }

    m_session.setRecorder(&m_recorder);

    connect(&m_recorder, &QMediaRecorder::recorderStateChanged,
            this, [this](QMediaRecorder::RecorderState state) {
        if (state == QMediaRecorder::StoppedState)
            emit encodingFinished(m_outputPath);
    });

    connect(&m_recorder, &QMediaRecorder::errorOccurred,
            this, [this](QMediaRecorder::Error /*err*/, const QString& msg) {
        emit encodingFailed(msg);
    });
}

bool VideoEncoder::start(const QString& outputPath, QSize frameSize)
{
    m_outputPath = outputPath;

    QMediaFormat format;
    if (m_settings.format == OutputFormat::WebM) {
        format.setFileFormat(QMediaFormat::WebM);
        format.setVideoCodec(QMediaFormat::VideoCodec::VP8);
    } else {
        format.setFileFormat(QMediaFormat::MPEG4);
        format.setVideoCodec(QMediaFormat::VideoCodec::H264);
    }

    m_recorder.setMediaFormat(format);
    m_recorder.setOutputLocation(QUrl::fromLocalFile(outputPath));
    m_recorder.setVideoResolution(frameSize);
    m_recorder.setVideoFrameRate(m_settings.fps);

    // Quality: use CRF-based encoding (quality enum → FFMPEG CRF) rather than
    // CBR. Setting both quality and bitrate causes Qt's FFMPEG backend to use
    // CBR and silently ignore the quality hint. CRF produces sharper output
    // for screen content because it allocates more bits to complex frames and
    // fewer to static ones.
    // VeryHighQuality → CRF ~10 for H264 (visually lossless for screen content).
    // Don't call setVideoBitRate() — let the encoder choose the bitrate.
    QMediaRecorder::Quality qtQuality;
    switch (m_settings.quality) {
    case QualityPreset::Low:    qtQuality = QMediaRecorder::LowQuality;      break;
    case QualityPreset::High:   qtQuality = QMediaRecorder::VeryHighQuality; break;
    case QualityPreset::Medium:
    default:                    qtQuality = QMediaRecorder::HighQuality;     break;
    }
    m_recorder.setQuality(qtQuality);

    qDebug("[VideoEncoder] start: %dx%d @ %dfps  quality=%d (CRF mode)",
           frameSize.width(), frameSize.height(), m_settings.fps,
           static_cast<int>(qtQuality));

    m_elapsed.start();
    m_recorder.record();

    if (m_recorder.error() != QMediaRecorder::NoError) {
        qWarning() << "VideoEncoder: failed to start recorder:" << m_recorder.errorString();
        return false;
    }
    return true;
}

void VideoEncoder::sendFrame(const QImage& image)
{
    // Convert to ARGB32 — universally accepted by Qt Multimedia and platform
    // codecs, avoiding a lossy secondary conversion inside QVideoFrame.
    const QImage converted = image.format() == QImage::Format_ARGB32
        ? image
        : image.convertToFormat(QImage::Format_ARGB32);

    QVideoFrame frame(converted);
    const qint64 pts = m_elapsed.nsecsElapsed() / 1000; // nanoseconds → microseconds
    frame.setStartTime(pts);
    frame.setEndTime(pts + 1'000'000 / m_settings.fps);
    m_input.sendVideoFrame(frame);
}

void VideoEncoder::stop()
{
    m_recorder.stop();
}

} // namespace sc
