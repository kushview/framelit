#pragma once

// Excludes a window from being captured by screen-recording APIs.
// On macOS this sets NSWindow.sharingType = NSWindowSharingNone.
// On other platforms this is a no-op.
//
// Call once, deferred via QTimer::singleShot(0,...) from showEvent — the
// NSWindow handle is not valid until after the first paint cycle.
void excludeWindowFromScreenCapture(void* nativeWindowHandle);
void setWindowCaptureExcluded(void* nativeWindowHandle, bool excluded);

// Proactively checks screen recording permission and triggers the macOS TCC
// consent prompt if not yet granted. Call once at app startup (before any
// QScreenCapture usage) so the system dialog appears at a predictable moment
// rather than racing with the first recording attempt.
// Returns true if permission is already granted, false otherwise.
bool requestScreenRecordingPermission();

// Returns the CGWindowID (window server ID) for a native macOS window handle.
// nativeWindowHandle is QWidget::winId() cast to void* — i.e. an NSView*.
// Must be called on the main thread.
unsigned int cgWindowIdForNativeHandle(void* nativeWindowHandle);
