#include "windowhelpers.hpp"

#ifdef Q_OS_MACOS
#include "macos_window.h"
#endif
#ifdef Q_OS_WIN
#include <windows.h>
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
    // via XShape in setWindowClickThrough() which is called elsewhere as needed.
}

} // namespace sc
