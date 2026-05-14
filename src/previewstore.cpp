#include "previewstore.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>

namespace sc {

PreviewStore::PreviewStore(QObject* parent)
    : QObject(parent)
{
}

ClipState PreviewStore::loadClipState(const QString& outputDir, const QString& clipPath)
{
    ClipState state;
    if (outputDir.isEmpty() || clipPath.isEmpty())
        return state;

    const QFileInfo clipInfo(clipPath);
    if (!clipInfo.exists())
        return state;

    const QJsonObject manifest = readManifest(outputDir);
    const QJsonArray clips = manifest.value("clips").toArray();

    const QString relativePath = clipInfo.fileName();
    const qint64 fileSize = clipInfo.size();
    const qint64 modTimeUtc = clipInfo.lastModified().toMSecsSinceEpoch();

    for (const QJsonValue& clip : clips) {
        const QJsonObject clipObj = clip.toObject();
        if (clipObj.value("path").toString() == relativePath &&
            clipObj.value("fileSize").toInteger() == fileSize &&
            clipObj.value("lastModifiedUtc").toInteger() == modTimeUtc) {
            state.inMs = clipObj.value("inMs").toInteger(0);
            state.outMs = clipObj.value("outMs").toInteger(0);
            state.positionMs = clipObj.value("positionMs").toInteger(0);
            return state;
        }
    }

    return state;
}

void PreviewStore::saveClipState(const QString& outputDir, const QString& clipPath, const ClipState& state)
{
    if (outputDir.isEmpty() || clipPath.isEmpty())
        return;

    const QFileInfo clipInfo(clipPath);
    if (!clipInfo.exists())
        return;

    QJsonObject manifest = readManifest(outputDir);
    QJsonArray clips = manifest.value("clips").toArray();

    const QString relativePath = clipInfo.fileName();
    const qint64 fileSize = clipInfo.size();
    const qint64 modTimeUtc = clipInfo.lastModified().toMSecsSinceEpoch();

    int existingIdx = -1;
    for (int i = 0; i < clips.count(); ++i) {
        const QJsonObject clipObj = clips[i].toObject();
        if (clipObj.value("path").toString() == relativePath &&
            clipObj.value("fileSize").toInteger() == fileSize &&
            clipObj.value("lastModifiedUtc").toInteger() == modTimeUtc) {
            existingIdx = i;
            break;
        }
    }

    QJsonObject clipObj;
    clipObj.insert("path", relativePath);
    clipObj.insert("fileSize", fileSize);
    clipObj.insert("lastModifiedUtc", modTimeUtc);
    clipObj.insert("inMs", state.inMs);
    clipObj.insert("outMs", state.outMs);
    clipObj.insert("positionMs", state.positionMs);

    if (existingIdx >= 0) {
        clips[existingIdx] = clipObj;
    } else {
        clips.append(clipObj);
    }

    manifest.insert("version", 1);
    manifest.insert("clips", clips);

    writeManifest(outputDir, manifest);
}

QString PreviewStore::manifestPath(const QString& outputDir) const
{
    return QDir(outputDir).filePath(QStringLiteral(".framelit-preview.json"));
}

QJsonObject PreviewStore::readManifest(const QString& outputDir) const
{
    const QString path = manifestPath(outputDir);
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly))
        return QJsonObject();

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (doc.isObject())
        return doc.object();

    return QJsonObject();
}

void PreviewStore::writeManifest(const QString& outputDir, const QJsonObject& manifest) const
{
    const QString path = manifestPath(outputDir);
    QFile file(path);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;

    const QJsonDocument doc(manifest);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
}

} // namespace sc
