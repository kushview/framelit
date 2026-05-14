#pragma once

#include <qwindowdefs.h>  // WId

namespace sc {

// Sets or clears an empty XShape input region on the window, making it
// fully click-through at the X server level when enabled is true.
// This is the Linux/X11 equivalent of NSWindow.ignoresMouseEvents.
//
// When enabled=false the input shape is reset to the bounding region
// (i.e. normal hit-testing is restored).
//
// Must be called on the main thread. No-op if the X SHAPE extension is
// not available on the current display.

} // namespace sc
