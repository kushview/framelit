#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>

namespace sc {

struct ClipState {
    qint64 inMs = 0;
    qint64 outMs = 0;
    qint64 positionMs = 0;
};

// Simple JSON-based store for per-clip state (in/out points, playhead position).
// Stores a single `.framelit-preview.json` manifest in the output directory.
class PreviewStore : public QObject {
    Q_OBJECT
public:
    explicit PreviewStore(QObject* parent = nullptr);

    // Load clip state from the manifest. Returns empty state if file not found.
    ClipState loadClipState(const QString& outputDir, const QString& clipPath);

    // Save clip state to the manifest.
    void saveClipState(const QString& outputDir, const QString& clipPath, const ClipState& state);

private:
    QString manifestPath(const QString& outputDir) const;
    QJsonObject readManifest(const QString& outputDir) const;
    void writeManifest(const QString& outputDir, const QJsonObject& manifest) const;
    QJsonObject makeClipKey(const QString& clipPath) const;
};

} // namespace sc
