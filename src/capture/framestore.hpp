#pragma once

#include "../appcontroller.hpp"

#include <QMutex>
#include <QObject>
#include <QRect>
#include <QVector>
#include <QVideoFrame>

namespace sc {

// A tagged frame: the raw video frame plus the capture region at the moment
// it was captured. Recording with a moving window means each frame may have
// a different crop rect — storing it per-frame ensures the encoder always
// crops accurately without needing to know the capture history.
struct TaggedFrame {
    QVideoFrame  frame;
    CaptureRegion region;   // logical coords + screen pointer at capture time
};

// Thread-safe append-only store for captured video frames.
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
    // Takes ownership of the QVideoFrame (ref-counted, cheap to copy).
    void addFrame(const QVideoFrame& frame, const CaptureRegion& region);

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
