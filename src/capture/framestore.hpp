#pragma once

#include "../appcontroller.hpp"

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QRect>
#include <QThread>
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
    // Frames are dropped once the byte budget (see setMaxBytes) is exceeded, so
    // an over-long buffered recording truncates instead of exhausting RAM.
    void addFrame(const QImage& image, const CaptureRegion& region);

    // Byte budget for buffered frames. Default ≈ 1.5 GiB. Frames beyond it are
    // dropped and bufferLimitReached() fires once. 0 = unlimited.
    void setMaxBytes(qint64 maxBytes);

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
    // Emitted on every kept addFrame() call. Carries the running frame count.
    // Connected to AppController::onFrameBuffered for UI progress updates.
    void frameBuffered(int totalCount);

    // Emitted exactly once, the first time a frame is dropped because the byte
    // budget was exceeded. Lets the UI warn that the recording was truncated.
    void bufferLimitReached();

private:
    static constexpr qint64 kDefaultMaxBytes = qint64(1536) * 1024 * 1024; // ~1.5 GiB

    mutable QMutex       m_mutex;
    QVector<TaggedFrame> m_frames;
    int                  m_totalAdded    = 0;
    qint64               m_currentBytes  = 0;
    qint64               m_maxBytes      = kDefaultMaxBytes;
    bool                 m_limitReached  = false;
    QThread*             m_producerThread = nullptr;  // set on first addFrame(); used for assertions
};

} // namespace sc
