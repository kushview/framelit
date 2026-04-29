#pragma once

#include "../appcontroller.hpp"

#include <QObject>
#include <QString>

namespace sc {

class FrameStore;

struct GifExportSettings {
    int outputFps = 10;    // target playback FPS (≤ recording FPS)
    int maxWidth  = 800;   // 0 = no scaling
    QualityPreset quality = QualityPreset::Medium;
};

// Encodes a FrameStore snapshot to an animated GIF file.
// Designed to run on a dedicated QThread. Call encode() via a queued invoke.
//
// Threading: created on the main thread, moved to an encoder thread, then
// encode() is invoked via QMetaObject::invokeMethod with QueuedConnection.
// Signals are emitted back to the main thread via Auto/QueuedConnection.
class GifEncoder : public QObject {
    Q_OBJECT

public:
    explicit GifEncoder(FrameStore* store,
                        const GifExportSettings& gifSettings,
                        int recordingFps,
                        const QString& outputPath,
                        QObject* parent = nullptr);

public slots:
    void encode();

signals:
    void progress(float fraction);   // 0.0 – 1.0
    void finished(QString filePath);
    void failed(QString reason);

private:
    FrameStore*       m_store;
    GifExportSettings m_gifSettings;
    int               m_recordingFps;
    QString           m_outputPath;
};

} // namespace sc
