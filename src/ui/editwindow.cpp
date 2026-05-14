
#include "editwindow.hpp"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QGraphicsItemGroup>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QMovie>
#include <QPushButton>
#include <QResizeEvent>
#include <QSlider>
#include <QSplitter>
#include <QStyle>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>

namespace sc {

void EditWindow::setAudioOutputDevice(const QString& deviceId)
{
    if (!m_player)
        return;

    QAudioDevice targetDevice;
    for (const QAudioDevice& dev : QMediaDevices::audioOutputs()) {
        if (dev.id() == deviceId) {
            targetDevice = dev;
            break;
        }
    }

    if (!targetDevice.isNull()) {
        auto* audioOut = new QAudioOutput(targetDevice, this);
        m_player->setAudioOutput(audioOut);
    }
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

EditWindow::EditWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("Framelit Preview"));
    resize(1100, 700);

    buildUi();
    syncTransportState();
}

void EditWindow::setOutputDir(const QString& dir)
{
    if (m_outputDir == dir)
        return;
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
    emit closed();
    QWidget::closeEvent(event);
}

void EditWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    fitMediaToView();
}

void EditWindow::buildUi()
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

    m_previewView = new QGraphicsView(center);
    m_previewView->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    m_previewView->setFrameShape(QFrame::NoFrame);
    m_previewView->setStyleSheet(QStringLiteral("background: #0f172a;"));

    m_scene = new QGraphicsScene(m_previewView);
    m_previewView->setScene(m_scene);

    m_mediaLayer = m_scene->createItemGroup({});
    m_mediaLayer->setZValue(0);
    m_annotationLayer = m_scene->createItemGroup({});
    m_annotationLayer->setZValue(100);
    m_mediaItem = new QGraphicsPixmapItem();
    m_mediaLayer->addToGroup(m_mediaItem);

    centerLayout->addWidget(m_previewView, 1);

    auto* transport = new QHBoxLayout();
    transport->setContentsMargins(0, 0, 0, 0);
    transport->setSpacing(8);

    m_playPauseButton = new QPushButton(QStringLiteral("Play"), center);
    m_stopButton = new QPushButton(QStringLiteral("Stop"), center);
    m_seekSlider = new QSlider(Qt::Horizontal, center);
    m_seekSlider->setRange(0, 0);
    m_timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), center);
    m_timeLabel->setMinimumWidth(110);

    transport->addWidget(m_playPauseButton);
    transport->addWidget(m_stopButton);
    transport->addWidget(m_seekSlider, 1);
    transport->addWidget(m_timeLabel);

    centerLayout->addLayout(transport);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({260, 840});

    root->addWidget(splitter, 1);

    auto* audioOut = new QAudioOutput(this);
    m_player = new QMediaPlayer(this);
    m_player->setAudioOutput(audioOut);
    m_videoSink = new QVideoSink(this);
    m_player->setVideoSink(m_videoSink);

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
        else
            m_player->play();
        syncTransportState();
    });

    connect(m_stopButton, &QPushButton::clicked, this, [this]() {
        if (m_isGif) {
            if (m_movie)
                m_movie->stop();
        } else {
            m_player->stop();
        }
        syncTransportState();
    });

    connect(m_seekSlider, &QSlider::sliderPressed, this, [this]() {
        m_isUserSeeking = true;
    });
    connect(m_seekSlider, &QSlider::sliderReleased, this, [this]() {
        m_isUserSeeking = false;
        if (!m_isGif)
            m_player->setPosition(m_seekSlider->value());
    });

    connect(m_videoSink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame& frame) {
        if (!frame.isValid())
            return;
        updateSceneFrame(frame.toImage());
    });

    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        m_durationMs = duration;
        m_seekSlider->setRange(0, int(duration));
        updateTimeLabel(m_player->position(), m_durationMs);
    });

    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        if (!m_isUserSeeking)
            m_seekSlider->setValue(int(pos));
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

    m_fileList->clear();

    if (m_outputDir.isEmpty())
        return;

    const QDir dir(m_outputDir);
    if (!dir.exists())
        return;

    const QFileInfoList files = dir.entryInfoList(
        QDir::Files | QDir::Readable,
        QDir::Time);

    for (const QFileInfo& fi : files) {
        if (!isSupportedPreviewSuffix(fi.suffix()))
            continue;
        auto* item = new QListWidgetItem(fi.fileName(), m_fileList);
        item->setData(Qt::UserRole, fi.absoluteFilePath());
    }

    if (m_fileList->count() > 0)
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
    m_seekSlider->setRange(0, 0);
    m_seekSlider->setValue(0);
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
        m_movie->start();
    } else {
        m_player->setSource(QUrl::fromLocalFile(path));
        m_player->play();
    }

    syncTransportState();
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
    m_seekSlider->setRange(0, 0);
    m_seekSlider->setValue(0);
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
        m_seekSlider->setEnabled(false);
        if (!m_movie || m_movie->state() == QMovie::NotRunning)
            m_playPauseButton->setText(QStringLiteral("Play"));
        else if (m_movie->state() == QMovie::Paused)
            m_playPauseButton->setText(QStringLiteral("Resume"));
        else
            m_playPauseButton->setText(QStringLiteral("Pause"));
    } else {
        const auto state = m_player->playbackState();
        m_seekSlider->setEnabled(m_durationMs > 0);
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
