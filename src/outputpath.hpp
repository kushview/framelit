#pragma once

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QString>

namespace sc {

inline QString resolvedOutputDir(const QString& configuredOutputDir)
{
    return configuredOutputDir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
        : configuredOutputDir;
}

inline QString makeCaptureOutputPath(const QString& configuredOutputDir,
                                     const QString& extension)
{
    const QString outputDir = resolvedOutputDir(configuredOutputDir);
    QDir().mkpath(outputDir);

    const QString timestamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd-HHmmss"));
    return outputDir + QDir::separator() +
           QStringLiteral("capture-%1.%2").arg(timestamp, extension);
}

} // namespace sc
