#include "x11_window.hpp"

// Qt headers MUST come before any X11 headers because Xlib.h defines macros
// (Bool, Status, None, ...) that collide with Qt's template machinery.
#include <qwindowdefs.h>

// X11 after Qt
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>

namespace sc {

void setX11WindowClickThrough(WId wid, bool enabled)
{
    Display* display = XOpenDisplay(nullptr);
    if (!display)
        return;

    // Check that the SHAPE extension is available (it always is on modern
    // X servers, but be safe).
    int eventBase, errorBase;
    if (!XShapeQueryExtension(display, &eventBase, &errorBase)) {
        XCloseDisplay(display);
        return;
    }

    if (enabled) {
        // Set an empty input shape — the X server will pass all pointer and
        // keyboard events through to whatever window is behind ours.
        XShapeCombineRectangles(display,
                                static_cast<Window>(wid),
                                ShapeInput,
                                0, 0,       // x, y offset
                                nullptr,    // rectangles (none)
                                0,          // n rectangles
                                ShapeSet,
                                Unsorted);
    } else {
        // Reset the input shape to the bounding shape (normal hit-testing).
        XShapeCombineMask(display,
                          static_cast<Window>(wid),
                          ShapeInput,
                          0, 0,
                          None,       // None = use bounding shape
                          ShapeSet);
    }

    XFlush(display);
    XCloseDisplay(display);
}

} // namespace sc

