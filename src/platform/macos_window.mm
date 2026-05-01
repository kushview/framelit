#include "macos_window.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <AVFoundation/AVFoundation.h>
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

bool requestAccessibilityPermission()
{
    if (AXIsProcessTrusted())
        return true;
    // Passing kAXTrustedCheckOptionPrompt: true opens System Settings >
    // Privacy & Security > Accessibility so the user can add the app.
    NSDictionary* opts = @{ (__bridge id)kAXTrustedCheckOptionPrompt: @YES };
    AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)opts);
    return false;
}

bool requestMicrophonePermission()
{
    AVAuthorizationStatus status =
        [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    if (status == AVAuthorizationStatusAuthorized)
        return true;
    if (status == AVAuthorizationStatusNotDetermined)
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL) {}];
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

void setNSWindowLevel(void* nativeWindowHandle, int level)
{
    if (!nativeWindowHandle)
        return;
    NSView*   view   = reinterpret_cast<NSView*>(nativeWindowHandle);
    NSWindow* window = [view window];
    if (!window)
        return;
    window.level = level;
    // NSWindowCollectionBehaviorStationary prevents macOS from hiding the
    // window when another app activates, which is essential for the control bar.
    const bool stationary = (level > NSFloatingWindowLevel);
    NSWindowCollectionBehavior behavior = NSWindowCollectionBehaviorCanJoinAllSpaces
                                        | NSWindowCollectionBehaviorIgnoresCycle;
    if (stationary)
        behavior |= NSWindowCollectionBehaviorStationary;
    window.collectionBehavior = behavior;
    [window orderFrontRegardless];
}

void setWindowHidesOnDeactivate(void* nativeWindowHandle, bool hides)
{
    if (!nativeWindowHandle)
        return;
    NSView*   view   = reinterpret_cast<NSView*>(nativeWindowHandle);
    NSWindow* window = [view window];
    if (!window)
        return;
    if ([window isKindOfClass:[NSPanel class]])
        ((NSPanel*)window).hidesOnDeactivate = hides ? YES : NO;
}

void setWindowClickThrough(void* nativeWindowHandle, bool enabled)
{
    if (!nativeWindowHandle)
        return;
    NSView*   view   = reinterpret_cast<NSView*>(nativeWindowHandle);
    NSWindow* window = [view window];
    if (!window)
        return;
    window.ignoresMouseEvents = enabled ? YES : NO;
    if (enabled) {
        // NSStatusWindowLevel keeps us above all normal/floating windows.
        // NSWindowCollectionBehaviorStationary prevents macOS from hiding or
        // moving the window when another app activates — this is the key flag
        // that stops the window from disappearing on app switches.
        window.level = NSStatusWindowLevel;
        window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
                                  | NSWindowCollectionBehaviorStationary
                                  | NSWindowCollectionBehaviorIgnoresCycle;
        [window orderFrontRegardless];
    } else {
        // Restore normal floating behaviour.
        window.level = NSFloatingWindowLevel;
        window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
                                  | NSWindowCollectionBehaviorIgnoresCycle;
    }
}
