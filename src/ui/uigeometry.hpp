#pragma once

#include <QtGlobal>

namespace sc {

// Derive the height that pairs with `width` to preserve `aspect` (width/height),
// clamped to at least `minDimension`. This is the single source of truth for the
// aspect-locked resize used by the capture-window grip (ControlBar), the
// hotkey grow/shrink, and the snap-to-aspect action — previously three slightly
// divergent copies (one truncated, the others rounded; now all rounded).
//
// A non-positive `aspect` means "no lock"; the width is returned clamped to the
// minimum, leaving height for the caller to decide.
inline int heightForAspect(int width, double aspect, int minDimension)
{
    if (aspect <= 0.0)
        return qMax(minDimension, width);
    return qMax(minDimension, qRound(width / aspect));
}

} // namespace sc
