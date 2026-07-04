#include "windowhelpers.hpp"

#ifdef Q_OS_MACOS
#include "macos_window.hpp"
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#ifdef Q_OS_LINUX
#include "x11_window.hpp"
#endif

namespace sc {

void setupOverlayWindowOnShow(WId wid)
{
#ifdef Q_OS_MACOS
    void* nativeHandle = reinterpret_cast<void*>(wid);
    setWindowHidesOnDeactivate(nativeHandle, false);
    excludeWindowFromScreenCapture(nativeHandle);
#endif

#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(wid);
    if (hwnd) {
        BOOL result = SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
        if (!result) {
            qWarning("[setupOverlayWindowOnShow] SetWindowDisplayAffinity failed");
        }
    }
#endif

    // X11: No special setup needed in showEvent; click-through is handled
    // via XShape in setX11WindowClickThrough() through the common helper.
}

void setWindowClickThrough(WId wid, bool enabled)
{
#ifdef Q_OS_MACOS
    void* nativeHandle = reinterpret_cast<void*>(wid);
    ::setWindowClickThrough(nativeHandle, enabled);
#elif defined(Q_OS_LINUX)
    sc::setX11WindowClickThrough(wid, enabled);
#else
    // No-op on other platforms for now
    (void)wid;
    (void)enabled;
#endif
}

} // namespace sc
