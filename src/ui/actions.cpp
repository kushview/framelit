#include "actions.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDir>
#include <QSignalBlocker>

namespace sc {

Actions::Actions(QObject* parent)
    : QObject(parent)
{
    // Recording lifecycle
    record      = new QAction(QStringLiteral("Record"),  this);
    pauseResume = new QAction(QStringLiteral("Pause"),   this);
    stop        = new QAction(QStringLiteral("Stop"),    this);

    // Format — exclusive radio group
    m_formatGroup = new QActionGroup(this);
    m_formatGroup->setExclusive(true);
    formatGif = new QAction(QStringLiteral("GIF"), this);
    formatGif->setCheckable(true);
    formatMp4 = new QAction(QStringLiteral("MP4"), this);
    formatMp4->setCheckable(true);
    m_formatGroup->addAction(formatGif);
    m_formatGroup->addAction(formatMp4);

    // Toggle settings
    audio       = new QAction(QStringLiteral("Capture Audio"), this);
    audio->setCheckable(true);
    hiDpi       = new QAction(QStringLiteral("HiDPI 2x"), this);
    hiDpi->setCheckable(true);
    followMouse = new QAction(QStringLiteral("Follow Mouse"), this);
    followMouse->setCheckable(true);

    // One-shot actions
    snapAspect  = new QAction(QStringLiteral("Snap Aspect (16:9 / 9:16)"), this);
    openPreview = new QAction(QStringLiteral("Open Preview"), this);
    openOutputDir = new QAction(QStringLiteral("Open Output Folder"), this);
    preferences = new QAction(QStringLiteral("Preferences\u2026"), this);
    showHide    = new QAction(QString(), this);
    quit        = new QAction(QStringLiteral("Quit"), this);

    // Wire triggers → signals
    connect(record,      &QAction::triggered, this, &Actions::recordRequested);
    connect(pauseResume, &QAction::triggered, this, &Actions::pauseResumeRequested);
    connect(stop,        &QAction::triggered, this, &Actions::stopRequested);
    connect(showHide,    &QAction::triggered, this, &Actions::toggleUiRequested);
    connect(snapAspect,  &QAction::triggered, this, &Actions::snapAspectRequested);
    connect(openPreview, &QAction::triggered, this, &Actions::openPreviewRequested);
    connect(openOutputDir, &QAction::triggered, this, &Actions::openOutputDirRequested);
    connect(preferences, &QAction::triggered, this, &Actions::preferencesRequested);
    connect(quit,        &QAction::triggered, this, &Actions::quitRequested);

    connect(formatGif, &QAction::triggered, this,
            [this]() { emit formatChangeRequested(OutputFormat::Gif); });
    connect(formatMp4, &QAction::triggered, this,
            [this]() { emit formatChangeRequested(OutputFormat::Mp4); });

    connect(audio,       &QAction::toggled, this, &Actions::audioChangeRequested);
    connect(hiDpi,       &QAction::toggled, this, &Actions::hiDpiChangeRequested);
    connect(followMouse, &QAction::toggled, this, &Actions::followMouseChangeRequested);
}

void Actions::sync(AppState state,
                   const RecordingSettings& settings,
                   bool hasPreviewMedia,
                   bool followEnabled,
                   bool uiVisible)
{
    const bool idle   = (state == AppState::Idle || state == AppState::Preview);
    const bool active = (state == AppState::Recording || state == AppState::Paused);
    const bool paused = (state == AppState::Paused);

    record->setEnabled(idle);
    pauseResume->setEnabled(active);
    pauseResume->setText(paused ? QStringLiteral("Resume") : QStringLiteral("Pause"));
    stop->setEnabled(active);
    snapAspect->setEnabled(idle);
    Q_UNUSED(hasPreviewMedia);
    openPreview->setEnabled(idle);
    openOutputDir->setEnabled(!settings.outputDir.isEmpty() && QDir(settings.outputDir).exists());

    {
        QSignalBlocker bgif(*formatGif), bmp4(*formatMp4);
        formatGif->setEnabled(idle);
        formatMp4->setEnabled(idle);
        formatGif->setChecked(settings.format == OutputFormat::Gif);
        formatMp4->setChecked(settings.format != OutputFormat::Gif);
    }
    {
        QSignalBlocker b(*audio);
        audio->setEnabled(idle && settings.format != OutputFormat::Gif);
        audio->setChecked(settings.captureAudio);
    }
    {
        QSignalBlocker b(*hiDpi);
        hiDpi->setEnabled(idle);
        hiDpi->setChecked(settings.hiDpi);
    }
    {
        QSignalBlocker b(*followMouse);
        followMouse->setChecked(followEnabled);
    }

    showHide->setText(uiVisible
        ? QStringLiteral("Hide Capture UI")
        : QStringLiteral("Show Capture UI"));
}

} // namespace sc
