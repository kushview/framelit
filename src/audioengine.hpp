#pragma once

#include <QAudioDevice>

class QString;

namespace sc::audio {

// Persist IDs as hex text so QSettings storage is stable.
QString encodeDeviceId(const QByteArray& id);
QByteArray decodeDeviceId(const QString& persistedId);

QAudioDevice resolveInputDevice(const QString& persistedId);
QAudioDevice resolveOutputDevice(const QString& persistedId);

} // namespace sc::audio
