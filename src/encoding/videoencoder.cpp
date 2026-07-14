#include "videoencoder.hpp"
#include "audioengine.hpp"

#include <QDateTime>
#include <QDebug>
#include <QMediaDevices>
#include <QMediaFormat>
#include <QUrl>
#include <QVideoFrame>

#include <cmath>

namespace sc {

namespace {

// Target video bitrate (bits/sec) for average-bitrate (ABR) encoding.
//
// We deliberately use ABR with an explicit bitrate rather than Qt's
// constant-quality path. On macOS the only bundled H.264 encoder is
// h264_videotoolbox, and in Qt's constant-quality mode VideoToolbox is driven
// by a fixed qscale that ignores setVideoBitRate() entirely — it under-allocates
// bits on motion, producing visible compression artifacts. Setting ABR + a
// generous bitrate is the only in-stack lever that actually raises quality.
//
// The heuristic mirrors Qt's own bitrateForSettings(): bits-per-pixel scaled by
// resolution, then by framerate (doubling fps ~1.5x the data, not 2x, since
// inter-frame deltas shrink). High maps to Qt's top "VeryHigh" factor.
int targetVideoBitRate(QSize frameSize, int fps, QualityPreset quality)
{
    // bits-per-pixel at 30 fps (matches Qt's H.264/VP8 table columns).
    double bpp = 1.75; // Medium
    switch (quality) {
    case QualityPreset::Low:    bpp = 0.75; break;
    case QualityPreset::High:   bpp = 16.0;  break;
    case QualityPreset::Medium:
    default:                    bpp = 1.75; break;
    }

    double bitrate = bpp * frameSize.width() * frameSize.height();
    const double safeFps = fps > 0 ? fps : 30.0;
    bitrate *= std::pow(1.5, std::log2(safeFps / 30.0));
    return static_cast<int>(bitrate);
}

} // namespace

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
        if (m_settings.captureAudio)
            format.setAudioCodec(QMediaFormat::AudioCodec::Opus);
    } else {
        format.setFileFormat(QMediaFormat::MPEG4);
        format.setVideoCodec(QMediaFormat::VideoCodec::H264);
        if (m_settings.captureAudio)
            format.setAudioCodec(QMediaFormat::AudioCodec::AAC);
    }

    m_recorder.setMediaFormat(format);
    m_recorder.setOutputLocation(QUrl::fromLocalFile(outputPath));
    m_recorder.setVideoResolution(frameSize);
    m_recorder.setVideoFrameRate(m_settings.fps);

    // Quality: use average-bitrate (ABR) encoding with an explicit, resolution-
    // scaled bitrate. Qt's constant-quality path drives the only bundled macOS
    // H.264 encoder (h264_videotoolbox) at a fixed qscale that ignores
    // setVideoBitRate() and under-allocates bits on motion — the source of the
    // compression artifacts. ABR + a generous bitrate lets VideoToolbox's
    // hardware rate control hold quality through motion. See targetVideoBitRate().
    const int videoBitRate = targetVideoBitRate(frameSize, m_settings.fps, m_settings.quality);
    m_recorder.setEncodingMode(QMediaRecorder::AverageBitRateEncoding);
    m_recorder.setVideoBitRate(videoBitRate);

    // In ABR mode audio is also driven by its bitrate; give AAC/Opus an explicit
    // rate so it doesn't fall back to the unset (-1) default.
    if (m_settings.captureAudio)
        m_recorder.setAudioBitRate(128000);

    qDebug("[VideoEncoder] start: %dx%d @ %dfps  quality=%d  videoBitRate=%d bps (ABR)",
           frameSize.width(), frameSize.height(), m_settings.fps,
           static_cast<int>(m_settings.quality), videoBitRate);

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
