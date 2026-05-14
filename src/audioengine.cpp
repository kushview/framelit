#include "audioengine.hpp"

#include <QMediaDevices>
#include <QString>

namespace sc::audio {

QString encodeDeviceId(const QByteArray& id)
{
    return QString::fromLatin1(id.toHex());
}

QByteArray decodeDeviceId(const QString& persistedId)
{
    if (persistedId.isEmpty())
        return {};

    const QByteArray ascii = persistedId.toLatin1().trimmed();
    const QByteArray hexDecoded = QByteArray::fromHex(ascii);
    if (!hexDecoded.isEmpty() && hexDecoded.toHex() == ascii.toLower())
        return hexDecoded;

    // Backward compatibility: older settings stored UTF-8 string directly.
    return persistedId.toUtf8();
}

QAudioDevice resolveInputDevice(const QString& persistedId)
{
    const QByteArray wanted = decodeDeviceId(persistedId);
    if (wanted.isEmpty())
        return {};

    const auto devices = QMediaDevices::audioInputs();
    for (const QAudioDevice& dev : devices) {
        if (dev.id() == wanted)
            return dev;
    }
    return {};
}

QAudioDevice resolveOutputDevice(const QString& persistedId)
{
    const QByteArray wanted = decodeDeviceId(persistedId);
    if (wanted.isEmpty())
        return {};

    const auto devices = QMediaDevices::audioOutputs();
    for (const QAudioDevice& dev : devices) {
        if (dev.id() == wanted)
            return dev;
    }
    return {};
}

} // namespace sc::audio
