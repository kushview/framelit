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
    QMutexLocker lock(&m_mutex);
    return m_frames.at(index);
}

void FrameStore::clear()
{
    QMutexLocker lock(&m_mutex);
    m_frames.clear();
    m_totalAdded = 0;
}

int FrameStore::totalAdded() const
{
    QMutexLocker lock(&m_mutex);
    return m_totalAdded;
}

} // namespace sc
