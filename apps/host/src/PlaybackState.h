#pragma once

#include <QObject>

#include <algorithm>

class PlaybackState : public QObject
{
  Q_OBJECT

public:
  explicit PlaybackState(QObject *parent = nullptr)
    : QObject(parent)
  {
  }

  int frame() const { return m_frame; }
  int frameCount() const { return m_frameCount; }

  void setFrame(int frame)
  {
    int const clamped = qBound(0, frame, std::max(0, m_frameCount - 1));
    if (clamped == m_frame) {
      return;
    }
    m_frame = clamped;
    Q_EMIT frameChanged(m_frame);
  }

  void setFrameCount(int count)
  {
    int const next = std::max(0, count);
    if (next == m_frameCount) {
      return;
    }
    m_frameCount = next;
    Q_EMIT frameCountChanged(m_frameCount);
    if (m_frame >= m_frameCount) {
      setFrame(std::max(0, m_frameCount - 1));
    }
  }

Q_SIGNALS:
  void frameChanged(int frame);
  void frameCountChanged(int count);

private:
  int m_frame = 0;
  int m_frameCount = 0;
};
