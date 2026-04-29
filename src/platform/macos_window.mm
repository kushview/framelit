#include "macos_window.h"

#import <AppKit/AppKit.h>

void excludeWindowFromScreenCapture(void* nativeWindowHandle)
{
    if (!nativeWindowHandle)
        return;

    NSView*   view   = reinterpret_cast<NSView*>(nativeWindowHandle);
    NSWindow* window = [view window];
    if (!window)
        return;

    // NSWindowSharingNone = 0 — the window content is excluded from all
    // screen-sharing and screen-recording APIs, including QScreenCapture
    // and the macOS screenshot capture pipeline.
    window.sharingType = NSWindowSharingNone;
}
