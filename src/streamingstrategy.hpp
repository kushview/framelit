#pragma once

#include "recordingstrategy.hpp"

namespace sc {

class VideoEncoder;

// Streaming recording strategy for MP4 / WebM output.
//
// Feeds captured QImage frames directly into VideoEncoder (QVideoFrameInput +
// QMediaRecorder) as they arrive. No frame buffering — memory usage is flat
// regardless of recording duration.
//
// The encoder is started lazily on the first frame so we know the frame size.
class StreamingStrategy : public RecordingStrategy {
    Q_OBJECT

public:
    explicit StreamingStrategy(const RecordingSettings& settings,
                               QObject* parent = nullptr);

    void onFrame(const QImage& image, const CaptureRegion& region) override;
    void finish() override;

private:
    VideoEncoder* m_encoder = nullptr;
    bool          m_started = false;
    QString       m_outputPath;
};

} // namespace sc
