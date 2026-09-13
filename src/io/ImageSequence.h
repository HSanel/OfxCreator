#pragma once

#include <QCache>
#include <QImage>
#include <QString>
#include <QStringList>

class ImageSequence
{
public:
  bool setPath(QString const &path);
  void clear();

  QString path() const { return m_path; }
  int frameCount() const { return m_frames.size(); }
  bool isEmpty() const { return m_frames.isEmpty(); }
  QString framePath(int index) const;
  QImage loadFrame(int index) const;

private:
  static QStringList collectFrames(QString const &path);

  QString m_path;
  QStringList m_frames;
  mutable QCache<int, QImage> m_frameCache;
};
