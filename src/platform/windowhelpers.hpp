#pragma once

#include <qwindowdefs.h>  // WId

namespace sc {

// Common window setup for overlay widgets (CenterHandle, CloseButton, etc.).
// Call this from showEvent(QShowEvent*) to apply consistent platform-specific
// configuration: exclude from screen capture, prevent auto-hide on deactivate.
//
// Usage:
//   void MyOverlay::showEvent(QShowEvent* event) {
//       QWidget::showEvent(event);
//       WId wid = winId();
//       QTimer::singleShot(0, this, [wid]() {
//           setupOverlayWindowOnShow(wid);
//       });
//   }
void setupOverlayWindowOnShow(WId wid);

// Window level constants for macOS (used with setOverlayWindowLevel on macOS).
// On other platforms these are no-ops.
static constexpr int kOverlayWindowLevel = 3;        // NSFloatingWindowLevel
static constexpr int kStatusWindowLevel = 25;        // NSStatusWindowLevel

// Sets the window to be click-through (ignores mouse events) or not, in a platform-agnostic way.
// On macOS, wid is cast to void* and passed to the native implementation.
// On X11, calls the X11 implementation. On other platforms, this is a no-op.
void setWindowClickThrough(WId wid, bool enabled);

} // namespace sc
