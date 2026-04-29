#include "macos_window.h"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

bool requestScreenRecordingPermission()
{
    // CGPreflightScreenCaptureAccess / CGRequestScreenCaptureAccess are
    // available since macOS 10.15 (Catalina). They interact with TCC directly:
    // preflight returns the current grant status without prompting, and
    // request shows the system consent dialog if not yet decided.
    if (CGPreflightScreenCaptureAccess()) {
        return true; // already granted
    }
    // Trigger the "Allow to record your screen?" system prompt.
    // The call returns false initially (permission is async); the app should
    // check again after the user responds.
    CGRequestScreenCaptureAccess();
    return false;
}

unsigned int cgWindowIdForNativeHandle(void* nativeWindowHandle)
{
    if (!nativeWindowHandle) return 0;
    NSView*   view = reinterpret_cast<NSView*>(nativeWindowHandle);
    NSWindow* win  = [view window];
    return win ? (unsigned int)[win windowNumber] : 0;
}

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

void setWindowCaptureExcluded(void* nativeWindowHandle, bool excluded)
{
    if (!nativeWindowHandle)
        return;
    NSView*   view   = reinterpret_cast<NSView*>(nativeWindowHandle);
    NSWindow* window = [view window];
    if (!window)
        return;
    window.sharingType = excluded ? NSWindowSharingNone : NSWindowSharingReadOnly;
}
