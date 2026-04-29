#pragma once

#include "screencapturebackend.hpp"

#include <QList>
#include <QMutex>
#include <atomic>
#include <cstdint>

namespace sc {

// Opaque holder for SCStream + SckDelegate — defined only in sckbackend.mm
// so Objective-C types don't leak into C++ translation units.
struct SckState;

// ---------------------------------------------------------------------------
// SckScreenCaptureBackend — ScreenCaptureKit implementation (macOS 12.3+)
//
// Uses SCContentFilter(display:excludingWindows:) so that overlay windows
// listed in setExcludedWindowIds() are genuinely absent from every captured
// frame, unlike QScreenCapture which creates an SCStream without an
// exclusion list.
//
// Window IDs
//   setExcludedWindowIds() accepts Qt WId values (NSView* cast to quintptr).
//   Internally they are converted to CGWindowID on the caller's thread
//   (must be main thread, i.e. before moveToThread) and matched against
//   SCShareableContent.windows when the stream starts.
// ---------------------------------------------------------------------------
class SckScreenCaptureBackend : public ScreenCaptureBackend {
    Q_OBJECT

public:
    explicit SckScreenCaptureBackend(int fps, QObject* parent = nullptr);
    ~SckScreenCaptureBackend() override;

    void setScreen(QScreen* screen) override;
    void setExcludedWindowIds(const QList<WId>& wids) override;

    void startCapture() override;
    void stopCapture() override;  // blocks until fully stopped

private:
    QScreen*             m_screen = nullptr;
    QList<uint32_t>      m_excludedCGWindowIds;

    std::atomic<bool>    m_active{false};
    QMutex               m_stateMutex;
    SckState*            m_state  = nullptr;   // guarded by m_stateMutex

    // Called from the SCK delegate (any thread) — posts to this object's thread.
    void dispatchFrame(const QImage& img);
    void dispatchError(const QString& msg);

    friend struct SckDelegateCallbacks;  // Obj-C trampoline calls these
};

} // namespace sc
