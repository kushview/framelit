#pragma once

// Excludes a window from being captured by screen-recording APIs.
// On macOS this sets NSWindow.sharingType = NSWindowSharingNone.
// On other platforms this is a no-op.
//
// Call once, deferred via QTimer::singleShot(0,...) from showEvent — the
// NSWindow handle is not valid until after the first paint cycle.
void excludeWindowFromScreenCapture(void* nativeWindowHandle);
void setWindowCaptureExcluded(void* nativeWindowHandle, bool excluded);

// Sets the NSWindowLevel. Use NSFloatingWindowLevel (3) normally and
// NSStatusWindowLevel (25) to stay above all apps while click-through is active.
void setNSWindowLevel(void* nativeWindowHandle, int level);

// Prevents an NSPanel (Qt::Tool window) from auto-hiding when the app deactivates.
// Call this once after the window is shown.
void setWindowHidesOnDeactivate(void* nativeWindowHandle, bool hides);

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

// Makes the window ignore all mouse events (click-through) when enabled is true,
// or restores normal hit-testing when false.
// Uses NSWindow.ignoresMouseEvents — unlike WA_TransparentForMouseEvents this
// is enforced at the window server level, so clicks pass through to whatever
// is behind the window regardless of Qt's own event filtering.
void setWindowClickThrough(void* nativeWindowHandle, bool enabled);
