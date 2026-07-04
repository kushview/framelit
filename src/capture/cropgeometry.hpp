#pragma once

#include "../appcontroller.hpp" // CaptureRegion

#include <QRect>
#include <QScreen>
#include <QSize>

namespace sc {

// Compute the physical-pixel crop rect that maps a full-screen frame down to
// the logical capture region.
//
// The backend may deliver frames at logical (1x) or physical (dpr x)
// resolution depending on the capture config, so the scale is *measured* from
// the frame dimensions vs. the screen's logical geometry rather than assumed.
//
// The result is clamped to [0, 0, frameSize]. If it would be empty, the full
// frame rect is returned (callers then encode the whole frame).
//
// This is the single source of truth for crop math shared by the GIF encoder
// and the streaming (video) strategy — keeping GIF and MP4 output pixel-aligned.
inline QRect physicalCropRect(const CaptureRegion& region, QSize frameSize)
{
    const QRect screenLogical = region.screen
        ? region.screen->geometry()
        : QRect(0, 0, frameSize.width(), frameSize.height());

    const qreal scaleX = screenLogical.width()  > 0
        ? (qreal)frameSize.width()  / screenLogical.width()  : 1.0;
    const qreal scaleY = screenLogical.height() > 0
        ? (qreal)frameSize.height() / screenLogical.height() : 1.0;

    const QRect localLogical = region.rect.translated(-screenLogical.topLeft());
    QRect scaled(
        qRound(localLogical.x()      * scaleX),
        qRound(localLogical.y()      * scaleY),
        qRound(localLogical.width()  * scaleX),
        qRound(localLogical.height() * scaleY));

    scaled = scaled.intersected(QRect(0, 0, frameSize.width(), frameSize.height()));
    if (scaled.isEmpty())
        return QRect(0, 0, frameSize.width(), frameSize.height());
    return scaled;
}

} // namespace sc
