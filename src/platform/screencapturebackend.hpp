#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
#include <qwindowdefs.h>  // WId

class QScreen;

namespace sc {

// ---------------------------------------------------------------------------
// ScreenCaptureBackend — abstract display-capture pipeline
//
// Implementations:
//   SckScreenCaptureBackend  (macOS 12.3+) — direct ScreenCaptureKit,
//       honours excludedWindowIds via SCContentFilter so overlay windows
//       are absent from the output.
//
//   QtScreenCaptureBackend  (all other platforms) — thin wrapper around
//       QScreenCapture + QMediaCaptureSession + QVideoSink.
//       On Windows the compositor already respects SetWindowDisplayAffinity
//       (WDA_EXCLUDEFROMCAPTURE) set on our overlay windows.
//
// Threading model
//   The backend is created on the main thread, then moved to the worker
//   thread via QThread::moveToThread (as a child of ScreenCaptureWorker).
//   startCapture() / stopCapture() are called on the worker thread.
//   frameArrived() MAY be emitted from any thread — connect with
//   Qt::QueuedConnection or Qt::AutoConnection.
// ---------------------------------------------------------------------------
class ScreenCaptureBackend : public QObject {
    Q_OBJECT

public:
    explicit ScreenCaptureBackend(int fps, QObject* parent = nullptr)
        : QObject(parent), m_fps(fps)
    {}
    ~ScreenCaptureBackend() override = default;

    // Must be called before startCapture().
    virtual void setScreen(QScreen* screen) = 0;

    // On macOS/SCK: these windows are excluded from the capture content.
    // On other platforms: ignored (exclusion is handled at the OS level).
    // Must be called before startCapture().
    virtual void setExcludedWindowIds(const QList<WId>& wids) = 0;

    // Begin capture. May return before the first frame arrives (async start).
    // Errors are reported via errorOccurred().
    virtual void startCapture() = 0;

    // Stop capture. Blocks until the pipeline is fully torn down and no
    // further frameArrived() signals will be emitted.
    virtual void stopCapture() = 0;

    bool isRunning() const { return m_running; }

signals:
    // May fire on any thread.
    void frameArrived(const QImage& image);
    void errorOccurred(const QString& message);

protected:
    int  m_fps;
    bool m_running = false;
};

} // namespace sc
