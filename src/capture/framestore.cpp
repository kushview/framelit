#include "framestore.hpp"

#include <QMutexLocker>

namespace sc {

FrameStore::FrameStore(QObject* parent)
    : QObject(parent)
{}

void FrameStore::addFrame(const QImage& image, const CaptureRegion& region)
{
    int count;
    {
        QMutexLocker lock(&m_mutex);
        // Record the producer thread on first call so subsequent assertions can check it.
        if (!m_producerThread)
            m_producerThread = QThread::currentThread();
        Q_ASSERT(QThread::currentThread() == m_producerThread);
        m_frames.append(TaggedFrame{ image, region });
        m_totalAdded++;
        count = m_totalAdded;
    }
    emit frameBuffered(count);
}

int FrameStore::frameCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_frames.size();
}

TaggedFrame FrameStore::frameAt(int index) const
{
    // Must not be called from the producer thread while recording is active.
    Q_ASSERT(!m_producerThread || QThread::currentThread() != m_producerThread);
    QMutexLocker lock(&m_mutex);
    return m_frames.at(index);
}

void FrameStore::clear()
{
    // Must not be called while the producer is actively writing.
    Q_ASSERT(!m_producerThread || QThread::currentThread() != m_producerThread);
    QMutexLocker lock(&m_mutex);
    m_frames.clear();
    m_totalAdded = 0;
    m_producerThread = nullptr;  // reset so the next recording session can re-bind
}

int FrameStore::totalAdded() const
{
    QMutexLocker lock(&m_mutex);
    return m_totalAdded;
}

} // namespace sc
