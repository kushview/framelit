#include "gifencoder.hpp"
#include "../capture/cropgeometry.hpp"
#include "../capture/framestore.hpp"
#include "../imageutil.hpp"

#include <QImage>
#include <QDebug>
#include <QScreen>

#include <limits>

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
    // GIF delay is in centiseconds. Compute in floating point before rounding
    // so high output rates (>50 fps) don't collapse to the same integer delay.
    const int delayCentisec =
        qMax(1, qRound(100.0 / qMax(1, m_gifSettings.outputFps)));

    // Open output file.
    int gifError = GIF_OK;
    const QByteArray pathBytes = m_outputPath.toUtf8();

    const int outW = qMax(1, m_gifSettings.outputSize.width());
    const int outH = qMax(1, m_gifSettings.outputSize.height());
    int targetColors = 128;
    switch (m_gifSettings.quality) {
    case QualityPreset::Low:    targetColors = 64;  break;
    case QualityPreset::Medium: targetColors = 128; break;
    case QualityPreset::High:   targetColors = 256; break;
    }
    qDebug(m_gifSettings.useCurrentFrameSize
               ? "[GIF] output from frame size setting: %dx%d, colors=%d"
               : "[GIF] fixed output from settings: %dx%d, colors=%d",
           outW, outH, targetColors);

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
        img = img.copy(physicalCropRect(tf.region, img.size()));

        // Scale to output dimensions if needed.
        img = scaleImage(img, QSize(outW, outH), m_gifSettings.letterbox);

        // Quantize to indexed color and enforce a palette budget by remapping
        // excess colors to the nearest retained palette entry.
        QImage indexed = img.convertToFormat(QImage::Format_Indexed8,
                                             Qt::DiffuseDither |
                                             Qt::PreferDither);
        const QVector<QRgb> colorTable = indexed.colorTable();
        const int numColors = qMin(targetColors, colorTable.size());
        if (numColors <= 0) {
            EGifCloseFile(gif, &gifError);
            emit failed(QStringLiteral("No colors available after GIF quantization."));
            return;
        }

        if (colorTable.size() > numColors) {
            QVector<uchar> remap(colorTable.size());
            for (int src = 0; src < colorTable.size(); ++src) {
                if (src < numColors) {
                    remap[src] = static_cast<uchar>(src);
                    continue;
                }

                const QRgb c = colorTable[src];
                int best = 0;
                int bestDist = std::numeric_limits<int>::max();
                for (int dst = 0; dst < numColors; ++dst) {
                    const QRgb p = colorTable[dst];
                    const int dr = qRed(c) - qRed(p);
                    const int dg = qGreen(c) - qGreen(p);
                    const int db = qBlue(c) - qBlue(p);
                    const int dist = dr * dr + dg * dg + db * db;
                    if (dist < bestDist) {
                        bestDist = dist;
                        best = dst;
                    }
                }
                remap[src] = static_cast<uchar>(best);
            }

            for (int row = 0; row < outH; ++row) {
                auto* scanline = indexed.scanLine(row);
                for (int col = 0; col < outW; ++col)
                    scanline[col] = remap[scanline[col]];
            }
        }

        // Build giflib color map.
        ColorMapObject* cmap = GifMakeMapObject(numColors, nullptr);
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
