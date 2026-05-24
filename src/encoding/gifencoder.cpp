#include "gifencoder.hpp"
#include "../capture/framestore.hpp"
#include "../imageutil.hpp"

#include <QImage>
#include <QDebug>
#include <QScreen>

#include <gif_lib.h>

namespace sc {

GifEncoder::GifEncoder(FrameStore* store,
                       const GifExportSettings& gifSettings,
                       int recordingFps,
                       const QString& outputPath,
                       QObject* parent)
    : QObject(parent)
    , m_store(store)
    , m_gifSettings(gifSettings)
    , m_recordingFps(recordingFps)
    , m_outputPath(outputPath)
{}

void GifEncoder::encode()
{
    const int frameCount = m_store->frameCount();
    if (frameCount == 0) {
        emit failed(QStringLiteral("No frames to encode."));
        return;
    }

    // How many source frames to skip between output frames.
    // e.g. recorded at 30fps, output at 10fps → keep every 3rd frame.
    const int stride = qMax(1, m_recordingFps / qMax(1, m_gifSettings.outputFps));
    // GIF delay is in centiseconds.
    const int delayCentisec = qMax(1, 100 / qMax(1, m_gifSettings.outputFps));

    // Open output file.
    int gifError = GIF_OK;
    const QByteArray pathBytes = m_outputPath.toUtf8();

    // Helper: compute physical crop rect for a given tagged frame.
    // CaptureRegion.rect is in logical pixels, global screen coordinates.
    // QScreenCapture gives us the full screen in physical pixels, so we:
    //   1. Make the rect relative to the screen's top-left
    //   2. Scale by devicePixelRatio to get physical pixel coordinates
    //   3. Clamp to the actual image bounds
    auto computeCropRect = [](const TaggedFrame& tf, const QImage& img) -> QRect {
        const CaptureRegion& region = tf.region;
        const QRect screenLogical = region.screen
            ? region.screen->geometry()
            : QRect(0, 0, img.width(), img.height());

        // Compute the actual scale factor from measured frame vs. logical screen
        // dimensions. The backend may deliver frames at logical (1x) or physical
        // (dpr x) resolution depending on the SCK config — don't assume.
        const qreal scaleX = screenLogical.width()  > 0
            ? (qreal)img.width()  / screenLogical.width()  : 1.0;
        const qreal scaleY = screenLogical.height() > 0
            ? (qreal)img.height() / screenLogical.height() : 1.0;

        QRect localLogical = region.rect.translated(-screenLogical.topLeft());
        QRect scaled(
            qRound(localLogical.x()      * scaleX),
            qRound(localLogical.y()      * scaleY),
            qRound(localLogical.width()  * scaleX),
            qRound(localLogical.height() * scaleY)
        );
        scaled = scaled.intersected(QRect(0, 0, img.width(), img.height()));
        if (scaled.isEmpty())
            scaled = QRect(0, 0, img.width(), img.height());
        return scaled;
    };

    const int outW = qMax(1, m_gifSettings.outputSize.width());
    const int outH = qMax(1, m_gifSettings.outputSize.height());
    qDebug(m_gifSettings.useCurrentFrameSize
               ? "[GIF] output from frame size setting: %dx%d"
               : "[GIF] fixed output from settings: %dx%d",
           outW, outH);

    GifFileType* gif = EGifOpenFileName(pathBytes.constData(), false, &gifError);
    if (!gif) {
        emit failed(QStringLiteral("Could not open output file: %1").arg(m_outputPath));
        return;
    }

    EGifSetGifVersion(gif, true);  // GIF89a for animation support

    // Write logical screen descriptor.
    if (EGifPutScreenDesc(gif, outW, outH, 8, 0, nullptr) == GIF_ERROR) {
        EGifCloseFile(gif, &gifError);
        emit failed(QStringLiteral("EGifPutScreenDesc failed."));
        return;
    }

    // Netscape looping extension (loop forever).
    const unsigned char nsExt[] = {
        'N','E','T','S','C','A','P','E','2','.','0'
    };
    const unsigned char loopBlock[] = { 1, 0, 0 }; // loop count = 0 (infinite)
    EGifPutExtensionLeader(gif, APPLICATION_EXT_FUNC_CODE);
    EGifPutExtensionBlock(gif, sizeof(nsExt), nsExt);
    EGifPutExtensionBlock(gif, sizeof(loopBlock), loopBlock);
    EGifPutExtensionTrailer(gif);

    int outputFrameIndex = 0;
    int totalOutput = (frameCount + stride - 1) / stride;

    for (int i = 0; i < frameCount; i += stride) {
        const TaggedFrame& tf = m_store->frameAt(i);

        QImage img = tf.image;
        if (img.isNull())
            continue;

        // Crop using this frame's own region — handles a moving capture window.
        img = img.copy(computeCropRect(tf, img));

        // Scale to output dimensions if needed.
        img = scaleImage(img, QSize(outW, outH), m_gifSettings.letterbox);

        // Quantize to 256 colours using Qt's built-in dithering.
        QImage indexed = img.convertToFormat(QImage::Format_Indexed8,
                                             Qt::DiffuseDither |
                                             Qt::PreferDither);
        const QVector<QRgb> colorTable = indexed.colorTable();
        const int numColors = qMin(256, colorTable.size());

        // Build giflib color map.
        ColorMapObject* cmap = GifMakeMapObject(256, nullptr);
        if (!cmap) {
            EGifCloseFile(gif, &gifError);
            emit failed(QStringLiteral("GifMakeMapObject failed."));
            return;
        }
        for (int c = 0; c < numColors; ++c) {
            cmap->Colors[c].Red   = qRed(colorTable[c]);
            cmap->Colors[c].Green = qGreen(colorTable[c]);
            cmap->Colors[c].Blue  = qBlue(colorTable[c]);
        }
        // Pad unused entries.
        for (int c = numColors; c < 256; ++c)
            cmap->Colors[c] = { 0, 0, 0 };

        // Graphic Control Extension: set frame delay.
        GraphicsControlBlock gcb{};
        gcb.DisposalMode   = DISPOSAL_UNSPECIFIED;
        gcb.UserInputFlag  = false;
        gcb.DelayTime      = delayCentisec;
        gcb.TransparentColor = NO_TRANSPARENT_COLOR;

        unsigned char gcbBytes[4];
        EGifGCBToExtension(&gcb, gcbBytes);
        EGifPutExtension(gif, GRAPHICS_EXT_FUNC_CODE, sizeof(gcbBytes), gcbBytes);

        // Image descriptor.
        if (EGifPutImageDesc(gif, 0, 0, outW, outH, false, cmap) == GIF_ERROR) {
            GifFreeMapObject(cmap);
            EGifCloseFile(gif, &gifError);
            emit failed(QStringLiteral("EGifPutImageDesc failed on frame %1.").arg(i));
            return;
        }

        // Write pixel rows.
        for (int row = 0; row < outH; ++row) {
            const uchar* scanline = indexed.constScanLine(row);
            if (EGifPutLine(gif, const_cast<GifPixelType*>(scanline), outW) == GIF_ERROR) {
                GifFreeMapObject(cmap);
                EGifCloseFile(gif, &gifError);
                emit failed(QStringLiteral("EGifPutLine failed on frame %1 row %2.").arg(i).arg(row));
                return;
            }
        }

        GifFreeMapObject(cmap);

        ++outputFrameIndex;
        emit progress(float(outputFrameIndex) / float(totalOutput));
    }

    if (EGifCloseFile(gif, &gifError) == GIF_ERROR) {
        emit failed(QStringLiteral("EGifCloseFile failed (error %1).").arg(gifError));
        return;
    }

    emit finished(m_outputPath);
}

} // namespace sc
