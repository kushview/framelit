
#include "editwindow.hpp"
#include "audioengine.hpp"
#include "previewstore.hpp"
#include "ui/actions.hpp"
#include "ui/timelinerange.hpp"

#include <QAudioOutput>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsItemGroup>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QMovie>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QSplitter>
#include <QStyle>
#include <QToolBar>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>

namespace sc {

void EditWindow::setAudioOutputDevice(const QString& deviceId)
{
    if (!m_player)
        return;

    if (m_audioOutput) {
        m_player->setAudioOutput(nullptr);
        delete m_audioOutput;
        m_audioOutput = nullptr;
    }

    const QAudioDevice target = audio::resolveOutputDevice(deviceId);
    m_audioOutput = target.isNull()
        ? new QAudioOutput(this)
        : new QAudioOutput(target, this);
    m_player->setAudioOutput(m_audioOutput);
}

namespace {

QString formatTime(qint64 ms)
{
    const qint64 totalSeconds = qMax<qint64>(0, ms / 1000);
    const qint64 minutes = totalSeconds / 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

bool isSupportedPreviewSuffix(const QString& suffix)
{
    const QString s = suffix.toLower();
    return s == QStringLiteral("gif") ||
           s == QStringLiteral("mp4") ||
           s == QStringLiteral("webm");
}

} // namespace

EditWindow::EditWindow(Actions* actions, QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("Framelit Preview"));
    setFocusPolicy(Qt::StrongFocus);
    resize(1100, 700);

    buildUi(actions);
    syncTransportState();
}

EditWindow::~EditWindow()
{
    // Detach before QObject child teardown to avoid QtMultimedia dangling callbacks.
    if (m_player)
        m_player->setAudioOutput(nullptr);
    if (m_audioOutput) {
        delete m_audioOutput;
        m_audioOutput = nullptr;
    }
}

void EditWindow::setOutputDir(const QString& dir)
{
    if (m_outputDir != dir)
        m_outputDir = dir;
    refreshFileList();
}

void EditWindow::selectFile(const QString& path)
{
    if (!m_fileList)
        return;

    if (path.isEmpty()) {
        if (m_fileList->count() > 0)
            m_fileList->setCurrentRow(0);
        return;
    }

    for (int i = 0; i < m_fileList->count(); ++i) {
        QListWidgetItem* item = m_fileList->item(i);
        if (!item)
            continue;
        if (item->data(Qt::UserRole).toString() == path) {
            m_fileList->setCurrentRow(i);
            loadMediaFile(path);
            return;
        }
    }

    loadMediaFile(path);
}

void EditWindow::closeEvent(QCloseEvent* event)
{
    QWidget::closeEvent(event);
    emit closed();
}

void EditWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    fitMediaToView();
}

void EditWindow::buildUi(Actions* actions)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    m_fileList = new QListWidget(splitter);
    m_fileList->setMinimumWidth(240);

    auto* center = new QWidget(splitter);
    auto* centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(8);

    auto* toolbar = new QToolBar(center);
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setIconSize(QSize(16, 16));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar->setStyleSheet(QStringLiteral(
        "QToolBar { border: 0; spacing: 6px; background: transparent; padding: 0; }"
        "QToolButton { color: #cbd5e1; border: 1px solid #334155; border-radius: 4px;"
        " padding: 4px 10px; background: #111827; }"
        "QToolButton:hover { border-color: #64748b; color: #f8fafc; }"
        "QToolButton:pressed { background: #0f172a; }"
        "QToolButton:disabled { color: #64748b; border-color: #1f2937; }"));
    if (actions) {
        toolbar->addAction(actions->showHide);
        toolbar->addAction(actions->openOutputDir);
        toolbar->addAction(actions->preferences);
    }
    centerLayout->addWidget(toolbar);

    m_previewView = new QGraphicsView(center);
    m_previewView->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    m_previewView->setFrameShape(QFrame::NoFrame);
    m_previewView->setStyleSheet(QStringLiteral("background: #000000;"));

    m_scene = new QGraphicsScene(m_previewView);
    m_scene->setBackgroundBrush(Qt::black);
    m_previewView->setScene(m_scene);

    m_mediaLayer = m_scene->createItemGroup({});
    m_mediaLayer->setZValue(0);
    m_annotationLayer = m_scene->createItemGroup({});
    m_annotationLayer->setZValue(100);
    m_mediaItem = new QGraphicsPixmapItem();
    m_mediaLayer->addToGroup(m_mediaItem);

    centerLayout->addWidget(m_previewView, 1);

    m_playPauseButton = new QPushButton(QStringLiteral("Play"), center);
    m_stopButton = new QPushButton(QStringLiteral("Stop"), center);
    m_timeline = new TimelineRangeWidget(center);
    m_timeline->setEnabled(false);
    m_timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), center);
    m_timeLabel->setMinimumWidth(110);

    auto* transport = new QVBoxLayout();
    transport->setContentsMargins(0, 0, 0, 0);
    transport->setSpacing(4);

    auto* sliderRow = new QHBoxLayout();
    sliderRow->setContentsMargins(0, 0, 0, 0);
    sliderRow->setSpacing(8);
    sliderRow->addWidget(m_timeline, 1);
    sliderRow->addWidget(m_timeLabel);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(8);
    buttonRow->addWidget(m_playPauseButton);
    buttonRow->addWidget(m_stopButton);
    buttonRow->addStretch();

    transport->addLayout(sliderRow);
    transport->addLayout(buttonRow);

    centerLayout->addLayout(transport);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({260, 840});

    root->addWidget(splitter, 1);

    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);
    m_videoSink = new QVideoSink(this);
    m_player->setVideoSink(m_videoSink);
    m_store = new PreviewStore(this);

    // Space bar triggers play/pause regardless of which child widget has focus.
    auto* spaceShortcut = new QShortcut(Qt::Key_Space, this);
    spaceShortcut->setContext(Qt::WindowShortcut);
    connect(spaceShortcut, &QShortcut::activated, this, [this]() {
        if (m_playPauseButton && m_playPauseButton->isEnabled())
            m_playPauseButton->click();
    });

    connect(m_fileList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0)
            return;
        QListWidgetItem* item = m_fileList->item(row);
        if (!item)
            return;
        loadMediaFile(item->data(Qt::UserRole).toString());
    });

    connect(m_playPauseButton, &QPushButton::clicked, this, [this]() {
        if (m_currentFile.isEmpty())
            return;
        if (m_isGif) {
            if (!m_movie)
                return;
            if (m_movie->state() == QMovie::Running)
                m_movie->setPaused(true);
            else if (m_movie->state() == QMovie::Paused)
                m_movie->setPaused(false);
            else
                m_movie->start();
            syncTransportState();
            return;
        }

        if (m_player->playbackState() == QMediaPlayer::PlayingState)
            m_player->pause();
        else {
            const qint64 currentPos = m_player->position();
            const bool shouldJumpToIn =
                m_player->playbackState() == QMediaPlayer::StoppedState ||
                currentPos < m_inPointMs ||
                currentPos > m_outPointMs;
            if (shouldJumpToIn)
                m_player->setPosition(m_inPointMs);
            m_player->play();
        }
        syncTransportState();
    });

    connect(m_stopButton, &QPushButton::clicked, this, [this]() {
        if (m_isGif) {
            if (m_movie)
                m_movie->stop();
        } else {
            m_player->stop();
            m_player->setPosition(m_inPointMs);
            m_timeline->setPositionMs(m_inPointMs);
            updateTimeLabel(m_inPointMs, m_durationMs);
        }
        syncTransportState();
    });

    connect(m_timeline, &TimelineRangeWidget::positionChangeRequested, this, [this](qint64 pos) {
        if (!m_isGif)
            m_player->setPosition(pos);
        updateTimeLabel(pos, m_durationMs);
    });

    connect(m_timeline, &TimelineRangeWidget::previewPositionRequested, this, [this](qint64 pos) {
        if (m_isGif)
            return;

        if (!m_previewScrubbing) {
            m_previewScrubbing = true;
            m_restorePreviewPositionMs = m_player->position();
            m_resumeAfterPreviewScrub =
                (m_player->playbackState() == QMediaPlayer::PlayingState);
            if (m_resumeAfterPreviewScrub)
                m_player->pause();
        }

        m_player->setPosition(pos);
        updateTimeLabel(pos, m_durationMs);
    });

    connect(m_timeline, &TimelineRangeWidget::previewFinished, this, [this](qint64 restorePositionMs) {
        if (m_isGif)
            return;

        const bool resume = m_resumeAfterPreviewScrub;
        m_previewScrubbing = false;
        m_resumeAfterPreviewScrub = false;
        m_restorePreviewPositionMs = restorePositionMs;
        m_player->setPosition(m_restorePreviewPositionMs);
        m_timeline->clearPreviewScrub();
        updateTimeLabel(m_restorePreviewPositionMs, m_durationMs);

        if (resume)
            m_player->play();
    });

    connect(m_timeline, &TimelineRangeWidget::inPointChanged, this, [this](qint64 inMs) {
        m_inPointMs = inMs;
        if (!m_currentFile.isEmpty())
            m_store->saveClipState(m_outputDir, m_currentFile, {m_inPointMs, m_outPointMs, m_player->position()});
    });

    connect(m_timeline, &TimelineRangeWidget::outPointChanged, this, [this](qint64 outMs) {
        m_outPointMs = outMs;
        if (!m_currentFile.isEmpty())
            m_store->saveClipState(m_outputDir, m_currentFile, {m_inPointMs, m_outPointMs, m_player->position()});
    });

    connect(m_videoSink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame& frame) {
        if (!frame.isValid())
            return;
        updateSceneFrame(frame.toImage());
    });

    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        m_durationMs = duration;

        // If no saved in/out, default to full duration
        if (m_inPointMs == 0 && m_outPointMs == 0)
            m_outPointMs = duration;

        m_timeline->setDurationMs(duration);
        m_timeline->setInOutMs(m_inPointMs, m_outPointMs);
        updateTimeLabel(m_player->position(), m_durationMs);
    });

    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        if (!m_previewScrubbing)
            m_timeline->setPositionMs(pos);

        updateTimeLabel(pos, m_durationMs);
    });

    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState) {
        syncTransportState();
    });
}

void EditWindow::refreshFileList()
{
    if (!m_fileList)
        return;

    const QString preferredPath = m_currentFile;
    m_fileList->clear();

    if (m_outputDir.isEmpty())
        return;

    const QDir dir(m_outputDir);
    if (!dir.exists())
        return;

    const QFileInfoList files = dir.entryInfoList(
        QDir::Files | QDir::Readable,
        QDir::Time | QDir::Reversed);

    int preferredIndex = -1;
    for (const QFileInfo& fi : files) {
        if (!isSupportedPreviewSuffix(fi.suffix()))
            continue;
        auto* item = new QListWidgetItem(fi.fileName(), m_fileList);
        const QString absolutePath = fi.absoluteFilePath();
        item->setData(Qt::UserRole, absolutePath);
        if (absolutePath == preferredPath)
            preferredIndex = m_fileList->count() - 1;
    }

    if (m_fileList->count() == 0) {
        unloadMedia();
        return;
    }

    if (preferredIndex >= 0)
        m_fileList->setCurrentRow(preferredIndex);
    else
        m_fileList->setCurrentRow(0);
}

void EditWindow::loadMediaFile(const QString& path)
{
    if (path.isEmpty())
        return;

    if (m_currentFile == path)
        return;

    unloadMedia();
    m_currentFile = path;
    m_durationMs = 0;

    // Load saved clip state if available
    const auto clipState = m_store->loadClipState(m_outputDir, path);
    m_inPointMs = clipState.inMs;
    m_outPointMs = clipState.outMs;

    m_timeline->setDurationMs(0);
    m_timeline->setInOutMs(0, 0);
    m_timeline->setPositionMs(0);
    updateTimeLabel(0, 0);

    const QFileInfo fi(path);
    m_isGif = (fi.suffix().toLower() == QStringLiteral("gif"));

    if (m_isGif) {
        m_movie = new QMovie(path);
        m_movie->setCacheMode(QMovie::CacheAll);
        connect(m_movie, &QMovie::frameChanged, this, [this](int) {
            if (!m_movie)
                return;
            updateSceneFrame(m_movie->currentImage());
        });
        connect(m_movie, &QMovie::stateChanged, this, [this](QMovie::MovieState) {
            syncTransportState();
        });
        // Do not auto-start GIF playback
    } else {
        m_player->setSource(QUrl::fromLocalFile(path));
        // Do not auto-start video playback
    }

    syncTransportState();
}

void EditWindow::keyPressEvent(QKeyEvent* event)
{
    if (!event->isAutoRepeat()) {
        if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
            if (m_fileList) {
                QListWidgetItem* item = m_fileList->currentItem();
                if (item) {
                    const QString path = item->data(Qt::UserRole).toString();
                    if (!path.isEmpty()) {
                        const QFileInfo fi(path);
                        const auto answer = QMessageBox::question(
                            this,
                            QStringLiteral("Delete Media"),
                            QStringLiteral("Delete '%1'? This cannot be undone.").arg(fi.fileName()),
                            QMessageBox::Yes | QMessageBox::No,
                            QMessageBox::No);
                        if (answer == QMessageBox::Yes) {
                            QFile::remove(path);
                            refreshFileList();
                        }
                    }
                }
            }
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_I) {
            // Set in-point to current playhead position
            if (m_player && m_timeline && !m_currentFile.isEmpty()) {
                const qint64 pos = m_player->position();
                m_inPointMs = pos;
                m_timeline->setInOutMs(m_inPointMs, m_outPointMs);
                m_store->saveClipState(m_outputDir, m_currentFile, {m_inPointMs, m_outPointMs, pos});
            }
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_O) {
            // Set out-point to current playhead position
            if (m_player && m_timeline && !m_currentFile.isEmpty()) {
                const qint64 pos = m_player->position();
                m_outPointMs = pos;
                m_timeline->setInOutMs(m_inPointMs, m_outPointMs);
                m_store->saveClipState(m_outputDir, m_currentFile, {m_inPointMs, m_outPointMs, pos});
            }
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            // Jump to in-point (if set) or beginning, then play
            if (m_player && m_timeline) {
                const qint64 startPos = (m_inPointMs > 0) ? m_inPointMs : 0;
                m_player->setPosition(startPos);
                m_timeline->setPositionMs(startPos);
                m_player->play();
            }
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_J) {
            // Step backward 2 seconds
            if (m_player && !m_isGif) {
                const qint64 pos = qMax<qint64>(0, m_player->position() - 500);
                m_player->setPosition(pos);
                m_timeline->setPositionMs(pos);
                updateTimeLabel(pos, m_durationMs);
            }
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_K) {
            // Pause
            if (m_player) {
                m_player->pause();
            }
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_L) {
            // Resume/play forward
            if (m_player) {
                m_player->setPlaybackRate(1.0);
                m_player->play();
            }
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

void EditWindow::unloadMedia()
{
    if (m_movie) {
        m_movie->stop();
        delete m_movie;
        m_movie = nullptr;
    }

    if (m_player) {
        m_player->stop();
        m_player->setSource(QUrl());
    }

    m_mediaItem->setPixmap(QPixmap());
    m_scene->setSceneRect(QRectF());
    m_durationMs = 0;
    m_inPointMs = 0;
    m_outPointMs = 0;
    m_timeline->setDurationMs(0);
    m_timeline->setInOutMs(0, 0);
    m_timeline->setPositionMs(0);
    m_currentFile.clear();
    m_isGif = false;
    updateTimeLabel(0, 0);
}

void EditWindow::updateSceneFrame(const QImage& frame)
{
    if (frame.isNull())
        return;

    m_mediaItem->setPixmap(QPixmap::fromImage(frame));
    m_scene->setSceneRect(QRectF(QPointF(0, 0), QSizeF(frame.size())));
    fitMediaToView();
}

void EditWindow::fitMediaToView()
{
    if (!m_previewView || !m_scene)
        return;
    if (m_scene->sceneRect().isEmpty())
        return;
    m_previewView->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
}

void EditWindow::syncTransportState()
{
    const bool hasSelection = !m_currentFile.isEmpty();
    m_playPauseButton->setEnabled(hasSelection);
    m_stopButton->setEnabled(hasSelection);

    if (m_isGif) {
        m_timeline->setEnabled(false);
        if (!m_movie || m_movie->state() == QMovie::NotRunning)
            m_playPauseButton->setText(QStringLiteral("Play"));
        else if (m_movie->state() == QMovie::Paused)
            m_playPauseButton->setText(QStringLiteral("Resume"));
        else
            m_playPauseButton->setText(QStringLiteral("Pause"));
    } else {
        const auto state = m_player->playbackState();
        m_timeline->setEnabled(m_durationMs > 0);
        if (state == QMediaPlayer::PlayingState)
            m_playPauseButton->setText(QStringLiteral("Pause"));
        else if (state == QMediaPlayer::PausedState)
            m_playPauseButton->setText(QStringLiteral("Resume"));
        else
            m_playPauseButton->setText(QStringLiteral("Play"));
    }
}

void EditWindow::updateTimeLabel(qint64 positionMs, qint64 durationMs)
{
    m_timeLabel->setText(formatTime(positionMs) + QStringLiteral(" / ") + formatTime(durationMs));
}

} // namespace sc
