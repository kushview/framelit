#pragma once

// Excludes a window from being captured by screen-recording APIs.
// On macOS this sets NSWindow.sharingType = NSWindowSharingNone.
// On other platforms this is a no-op.
//
// Call once, deferred via QTimer::singleShot(0,...) from showEvent — the
// NSWindow handle is not valid until after the first paint cycle.
void excludeWindowFromScreenCapture(void* nativeWindowHandle);
