#pragma once

#include <QImage>
#include <QPainter>
#include <QSize>

namespace sc {

// Scale `src` to fit `targetSize`. When `letterbox` is true the image is
// scaled with KeepAspectRatio and centered on a black canvas. When false
// the image is stretched to fill without preserving the aspect ratio.
inline QImage scaleImage(const QImage& src, QSize targetSize, bool letterbox)
{
    if (src.size() == targetSize)
        return src;

    if (letterbox) {
        const QImage fitted = src.scaled(targetSize, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
        QImage composed(targetSize, QImage::Format_ARGB32);
        composed.fill(Qt::black);
        QPainter painter(&composed);
        painter.drawImage((targetSize.width()  - fitted.width())  / 2,
                          (targetSize.height() - fitted.height()) / 2,
                          fitted);
        return composed;
    }

    return src.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

} // namespace sc
