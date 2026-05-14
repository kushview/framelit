#include "ui/timelinerange.hpp"

#include <algorithm>
#include <QMouseEvent>
#include <QPainter>

namespace sc {

namespace {

constexpr int kOuterPad = 10;
constexpr int kTrackHeight = 14;
constexpr int kHandleWidth = 14;
constexpr int kMinGapMs = 1;
constexpr int kPlayheadTriangleHeight = 8;

} // namespace

TimelineRangeWidget::TimelineRangeWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(34);
    setMouseTracking(true);
}

void TimelineRangeWidget::setDurationMs(qint64 durationMs)
{
    m_durationMs = qMax<qint64>(0, durationMs);
    m_inMs = 0;
    m_outMs = m_durationMs;
    m_positionMs = qBound<qint64>(m_inMs, m_positionMs, m_outMs);
    m_previewPositionMs = m_positionMs;
    m_restorePositionMs = m_positionMs;
    m_previewScrubbing = false;
    update();
}

void TimelineRangeWidget::setPositionMs(qint64 positionMs)
{
    setPositionInternal(positionMs, false);
}

void TimelineRangeWidget::clearPreviewScrub()
{
    if (!m_previewScrubbing)
        return;

    m_previewScrubbing = false;
    m_previewPositionMs = m_positionMs;
    update();
}

void TimelineRangeWidget::setInOutMs(qint64 inMs, qint64 outMs)
{
    if (m_durationMs <= 0) {
        m_inMs = 0;
        m_outMs = 0;
        m_positionMs = 0;
        update();
        return;
    }

    inMs = qBound<qint64>(0, inMs, m_durationMs);
    outMs = qBound<qint64>(0, outMs, m_durationMs);
    if (outMs < inMs)
        std::swap(inMs, outMs);

    m_inMs = inMs;
    m_outMs = outMs;
    m_positionMs = qBound<qint64>(m_inMs, m_positionMs, m_outMs);
    if (!m_previewScrubbing)
        m_previewPositionMs = m_positionMs;
    update();
}

void TimelineRangeWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF tr = trackRect();

    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#0b1220"));
    p.drawRoundedRect(tr, 3, 3);

    if (m_durationMs > 0) {
        const qreal xIn = timeToX(m_inMs);
        const qreal xOut = timeToX(m_outMs);

        const QRectF active(QPointF(xIn, tr.top()), QPointF(xOut, tr.bottom()));
        p.setBrush(QColor("#7c7f85"));
        p.drawRoundedRect(active.normalized(), 2, 2);

        p.setPen(QPen(QColor("#94a3b8"), 1));
        for (qreal x = contentLeft(); x <= contentRight(); x += 28.0)
            p.drawLine(QPointF(x, tr.center().y() - 5), QPointF(x, tr.center().y() + 5));

        p.setPen(QPen(QColor("#60a5fa"), 1));
        p.setBrush(QColor("#60a5fa"));
        const QRectF inR = handleRectAt(xIn);
        const QRectF outR = handleRectAt(xOut);
        p.drawRoundedRect(inR, 2, 2);
        p.drawRoundedRect(outR, 2, 2);

        // Bracket glyphs: [ and ]
        p.setPen(QPen(QColor("#0f172a"), 2));
        const qreal iy = inR.top() + 3;
        const qreal ib = inR.bottom() - 3;
        p.drawLine(QPointF(inR.left() + 4, iy), QPointF(inR.left() + 4, ib));
        p.drawLine(QPointF(inR.left() + 4, iy), QPointF(inR.right() - 3, iy));
        p.drawLine(QPointF(inR.left() + 4, ib), QPointF(inR.right() - 3, ib));

        const qreal oy = outR.top() + 3;
        const qreal ob = outR.bottom() - 3;
        p.drawLine(QPointF(outR.right() - 4, oy), QPointF(outR.right() - 4, ob));
        p.drawLine(QPointF(outR.left() + 3, oy), QPointF(outR.right() - 4, oy));
        p.drawLine(QPointF(outR.left() + 3, ob), QPointF(outR.right() - 4, ob));

        const qreal xPos = timeToX(m_positionMs);
        p.setPen(QPen(QColor("#60a5fa"), 2));
        p.drawLine(QPointF(xPos, tr.top()), QPointF(xPos, tr.bottom()));

        QPolygonF triangle;
        triangle << QPointF(xPos, tr.top() - 1)
                 << QPointF(xPos - 6, tr.top() - kPlayheadTriangleHeight)
                 << QPointF(xPos + 6, tr.top() - kPlayheadTriangleHeight);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#60a5fa"));
        p.drawPolygon(triangle);

        if (m_previewScrubbing) {
            const qreal xPreview = timeToX(m_previewPositionMs);
            p.setPen(QPen(QColor("#f8fafc"), 1, Qt::DashLine));
            p.drawLine(QPointF(xPreview, tr.top() - 3), QPointF(xPreview, tr.bottom() + 3));
        }
    }

    p.setPen(QColor("#334155"));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(tr, 3, 3);
}

void TimelineRangeWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || m_durationMs <= 0) {
        QWidget::mousePressEvent(event);
        return;
    }

    const qreal x = event->position().x();
    const qreal xIn = timeToX(m_inMs);
    const qreal xOut = timeToX(m_outMs);
    const qreal xPos = timeToX(m_positionMs);

    const qreal dIn = qAbs(x - xIn);
    const qreal dOut = qAbs(x - xOut);
    const qreal dPos = qAbs(x - xPos);

    if (dIn <= kHandleWidth * 0.9) {
        m_dragTarget = DragTarget::InHandle;
    } else if (dOut <= kHandleWidth * 0.9) {
        m_dragTarget = DragTarget::OutHandle;
    } else if (dPos <= 8.0) {
        m_dragTarget = DragTarget::Playhead;
    } else {
        m_dragTarget = DragTarget::Playhead;
        setPositionInternal(xToTime(x), true);
    }

    if (m_dragTarget == DragTarget::InHandle || m_dragTarget == DragTarget::OutHandle) {
        m_previewScrubbing = true;
        m_restorePositionMs = m_positionMs;
        m_previewPositionMs = m_positionMs;
    }

    m_dragging = true;
    update();
}

void TimelineRangeWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging || m_durationMs <= 0) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const qint64 t = xToTime(event->position().x());

    switch (m_dragTarget) {
    case DragTarget::InHandle:
        setInInternal(qMin(t, m_outMs - kMinGapMs), true);
        setPreviewPositionInternal(m_inMs, true);
        break;
    case DragTarget::OutHandle:
        setOutInternal(qMax(t, m_inMs + kMinGapMs), true);
        setPreviewPositionInternal(m_outMs, true);
        break;
    case DragTarget::Playhead:
        setPositionInternal(t, true);
        break;
    case DragTarget::None:
        break;
    }
}

void TimelineRangeWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_previewScrubbing &&
            (m_dragTarget == DragTarget::InHandle || m_dragTarget == DragTarget::OutHandle)) {
            emit previewFinished(m_restorePositionMs);
            m_previewScrubbing = false;
            m_previewPositionMs = m_positionMs;
        }
        m_dragging = false;
        m_dragTarget = DragTarget::None;
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

qreal TimelineRangeWidget::contentLeft() const
{
    return kOuterPad;
}

qreal TimelineRangeWidget::contentRight() const
{
    return width() - kOuterPad;
}

qreal TimelineRangeWidget::timeToX(qint64 t) const
{
    if (m_durationMs <= 0)
        return contentLeft();

    const qreal ratio = qreal(t) / qreal(m_durationMs);
    return contentLeft() + (contentRight() - contentLeft()) * ratio;
}

qint64 TimelineRangeWidget::xToTime(qreal x) const
{
    if (m_durationMs <= 0)
        return 0;

    const qreal clampedX = qBound(contentLeft(), x, contentRight());
    const qreal ratio = (clampedX - contentLeft()) / (contentRight() - contentLeft());
    return qint64(ratio * qreal(m_durationMs));
}

QRectF TimelineRangeWidget::trackRect() const
{
    const qreal top = (height() - kTrackHeight) / 2.0;
    return QRectF(contentLeft(), top, contentRight() - contentLeft(), kTrackHeight);
}

QRectF TimelineRangeWidget::handleRectAt(qreal x) const
{
    const QRectF tr = trackRect();
    return QRectF(x - (kHandleWidth / 2.0), tr.top() - 4, kHandleWidth, tr.height() + 8);
}

qint64 TimelineRangeWidget::clampTime(qint64 t) const
{
    return qBound<qint64>(0, t, m_durationMs);
}

void TimelineRangeWidget::setPositionInternal(qint64 t, bool emitSignal)
{
    const qint64 next = qBound<qint64>(m_inMs, clampTime(t), m_outMs);
    if (next == m_positionMs)
        return;
    m_positionMs = next;
    if (!m_previewScrubbing)
        m_previewPositionMs = next;
    update();
    if (emitSignal)
        emit positionChangeRequested(m_positionMs);
}

void TimelineRangeWidget::setInInternal(qint64 t, bool emitSignal)
{
    const qint64 next = qBound<qint64>(0, t, m_outMs);
    if (next == m_inMs)
        return;
    m_inMs = next;
    if (m_positionMs < m_inMs)
        m_positionMs = m_inMs;
    update();
    if (emitSignal)
        emit inPointChanged(m_inMs);
}

void TimelineRangeWidget::setOutInternal(qint64 t, bool emitSignal)
{
    const qint64 next = qBound<qint64>(m_inMs, t, m_durationMs);
    if (next == m_outMs)
        return;
    m_outMs = next;
    if (m_positionMs > m_outMs)
        m_positionMs = m_outMs;
    update();
    if (emitSignal)
        emit outPointChanged(m_outMs);
}

void TimelineRangeWidget::setPreviewPositionInternal(qint64 t, bool emitSignal)
{
    const qint64 next = qBound<qint64>(0, clampTime(t), m_durationMs);
    if (next == m_previewPositionMs)
        return;

    m_previewPositionMs = next;
    update();
    if (emitSignal)
        emit previewPositionRequested(m_previewPositionMs);
}

} // namespace sc
