#pragma once

#include "screencapturebackend.hpp"

class QMediaCaptureSession;
class QScreenCapture;
class QVideoFrame;
class QVideoSink;

namespace sc {

// ---------------------------------------------------------------------------
// QtScreenCaptureBackend — QScreenCapture-based fallback for Windows / Linux.
//
// Wraps QScreenCapture + QMediaCaptureSession + QVideoSink; the frame
// pipeline is identical to the pre-backend ScreenCaptureWorker.
//
// setExcludedWindowIds() is a no-op: on Windows the compositor already
// honours SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) which is applied
// to overlay windows at creation time.  On Linux/X11 no compositor-level
// exclusion is available through this path.
// ---------------------------------------------------------------------------
class QtScreenCaptureBackend : public ScreenCaptureBackend {
    Q_OBJECT

public:
    explicit QtScreenCaptureBackend(int fps, QObject* parent = nullptr);
    ~QtScreenCaptureBackend() override;

    void setScreen(QScreen* screen) override;
    void setExcludedWindowIds(const QList<WId>& wids) override;   // no-op

    void startCapture() override;
    void stopCapture() override;

private slots:
    void onVideoFrameChanged(const QVideoFrame& frame);
    void onCaptureError(int code, const QString& message);

private:
    QScreen*              m_screen  = nullptr;
    QScreenCapture*       m_capture = nullptr;
    QMediaCaptureSession* m_session = nullptr;
    QVideoSink*           m_sink    = nullptr;

    bool m_errorReported = false;
};

} // namespace sc
