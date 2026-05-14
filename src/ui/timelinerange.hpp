#pragma once

#include <QWidget>

namespace sc {

// Simple editor-style timeline with a playhead and draggable [in][out] handles.
class TimelineRangeWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimelineRangeWidget(QWidget* parent = nullptr);

    void setDurationMs(qint64 durationMs);
    void setPositionMs(qint64 positionMs);
    void setInOutMs(qint64 inMs, qint64 outMs);
    void clearPreviewScrub();

    qint64 durationMs() const { return m_durationMs; }
    qint64 positionMs() const { return m_positionMs; }
    qint64 inMs() const { return m_inMs; }
    qint64 outMs() const { return m_outMs; }
    bool isPreviewScrubbing() const { return m_previewScrubbing; }

signals:
    void positionChangeRequested(qint64 positionMs);
    void previewPositionRequested(qint64 positionMs);
    void previewFinished(qint64 restorePositionMs);
    void inPointChanged(qint64 inMs);
    void outPointChanged(qint64 outMs);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum class DragTarget { None, Playhead, InHandle, OutHandle };

    qreal contentLeft() const;
    qreal contentRight() const;
    qreal timeToX(qint64 t) const;
    qint64 xToTime(qreal x) const;
    QRectF trackRect() const;
    QRectF handleRectAt(qreal x) const;
    qint64 clampTime(qint64 t) const;

    void setPositionInternal(qint64 t, bool emitSignal);
    void setInInternal(qint64 t, bool emitSignal);
    void setOutInternal(qint64 t, bool emitSignal);
    void setPreviewPositionInternal(qint64 t, bool emitSignal);

    qint64 m_durationMs = 0;
    qint64 m_positionMs = 0;
    qint64 m_previewPositionMs = 0;
    qint64 m_restorePositionMs = 0;
    qint64 m_inMs = 0;
    qint64 m_outMs = 0;

    DragTarget m_dragTarget = DragTarget::None;
    bool m_dragging = false;
    bool m_previewScrubbing = false;
};

} // namespace sc
