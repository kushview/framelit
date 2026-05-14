#pragma once

#include <QWidget>

class QListWidget;
class QListWidgetItem;
class QGraphicsPixmapItem;
class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QMediaPlayer;
class QPushButton;
class QVideoSink;
class QMovie;
class QGraphicsItemGroup;
class QAudioOutput;
class QToolBar;

namespace sc {

class Actions;
class PreviewStore;
class TimelineRangeWidget;

// Preview/edit surface for recorded media.
// Uses a composited QGraphicsScene with dedicated media + annotation layers,
// so annotations can be applied consistently across GIF/video in later phases.
class EditWindow : public QWidget {
    Q_OBJECT

public:
    explicit EditWindow(Actions* actions, QWidget* parent = nullptr);
    ~EditWindow() override;
    void setAudioOutputDevice(const QString& deviceId);

    void setOutputDir(const QString& dir);
    void selectFile(const QString& path);

    // Exposes the annotation layer for future tools without changing the
    // playback/compositing pipeline.
    QGraphicsItemGroup* annotationLayer() const { return m_annotationLayer; }

signals:
    void closed();

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void buildUi(Actions* actions);
    void refreshFileList();
    void loadMediaFile(const QString& path);
    void unloadMedia();
    void updateSceneFrame(const QImage& frame);
    void fitMediaToView();
    void syncTransportState();
    void updateTimeLabel(qint64 positionMs, qint64 durationMs);

    QString m_outputDir;
    QString m_currentFile;

    QListWidget* m_fileList = nullptr;
    QGraphicsView* m_previewView = nullptr;
    QGraphicsScene* m_scene = nullptr;
    QGraphicsItemGroup* m_mediaLayer = nullptr;
    QGraphicsItemGroup* m_annotationLayer = nullptr;
    QGraphicsPixmapItem* m_mediaItem = nullptr;

    QPushButton* m_playPauseButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    TimelineRangeWidget* m_timeline = nullptr;
    QLabel* m_timeLabel = nullptr;

    QMediaPlayer* m_player = nullptr;
    QAudioOutput* m_audioOutput = nullptr;
    QVideoSink* m_videoSink = nullptr;
    QMovie* m_movie = nullptr;
    PreviewStore* m_store = nullptr;

    qint64 m_durationMs = 0;
    qint64 m_inPointMs = 0;
    qint64 m_outPointMs = 0;
    qint64 m_restorePreviewPositionMs = 0;
    bool m_previewScrubbing = false;
    bool m_resumeAfterPreviewScrub = false;
    bool m_isGif = false;
};

} // namespace sc
