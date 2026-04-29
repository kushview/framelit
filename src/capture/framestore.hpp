#pragma once

#include "../appcontroller.hpp"

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QRect>
#include <QVector>

namespace sc {

// A tagged frame: pixel data plus the capture region at the moment it was
// captured. Storing QImage (not QVideoFrame) means the underlying
// CMSampleBuffer (macOS ScreenCaptureKit) is released immediately after
// capture, keeping the pipeline buffer pool free for continuous streaming.
struct TaggedFrame {
    QImage        image;
    CaptureRegion region;   // logical coords + screen pointer at capture time
};

// Thread-safe append-only store for captured frames.
//
// Producer: ScreenCaptureWorker (worker thread) calls addFrame().
// Consumer: GifEncoder / Mp4Encoder (encoder thread) calls frameCount() and
//           frameAt(i) at encode time — well after recording has stopped.
//
// During recording the main thread must not call frameAt() — only addFrame()
// and clear() are safe to call while the producer is running.
class FrameStore : public QObject {
    Q_OBJECT

public:
    explicit FrameStore(QObject* parent = nullptr);

    // Thread-safe. Called by the capture worker on every kept frame.
    void addFrame(const QImage& image, const CaptureRegion& region);

    // Safe to call from any thread once recording has stopped.
    int frameCount() const;
    TaggedFrame frameAt(int index) const;

    // Discard all frames. Call before re-recording.
    // Not safe to call while the producer is running.
    void clear();

    // Total number of frames ever added (does not reset on clear, used for
    // progress reporting during capture).
    int totalAdded() const;

signals:
    // Emitted on every addFrame() call. Carries the running frame count.
    // Connected to AppController::onFrameBuffered for UI progress updates.
    void frameBuffered(int totalCount);

private:
    mutable QMutex       m_mutex;
    QVector<TaggedFrame> m_frames;
    int                  m_totalAdded = 0;
};

} // namespace sc
