#include "ImageSequence.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace {

QStringList const &imageSuffixes()
{
  static QStringList const suffixes{QStringLiteral("png"),
                                    QStringLiteral("jpg"),
                                    QStringLiteral("jpeg"),
                                    QStringLiteral("bmp"),
                                    QStringLiteral("tif"),
                                    QStringLiteral("tiff"),
                                    QStringLiteral("webp")};
  return suffixes;
}

} // namespace

bool ImageSequence::setPath(QString const &path)
{
  m_frameCache.clear();
  m_frameCache.setMaxCost(48);
  m_path = path;
  m_frames = collectFrames(path);
  return !m_frames.isEmpty();
}

void ImageSequence::clear()
{
  m_path.clear();
  m_frames.clear();
  m_frameCache.clear();
}

QString ImageSequence::framePath(int index) const
{
  if (index < 0 || index >= m_frames.size()) {
    return {};
  }
  return m_frames.at(index);
}

QImage ImageSequence::loadFrame(int index) const
{
  if (QImage *cached = m_frameCache.object(index)) {
    return *cached;
  }

  QString const file = framePath(index);
  if (file.isEmpty()) {
    return {};
  }
  auto *image = new QImage;
  if (!image->load(file)) {
    delete image;
    return {};
  }
  QImage copy = *image;
  m_frameCache.insert(index, image, 1);
  return copy;
}

QStringList ImageSequence::collectFrames(QString const &path)
{
  if (path.isEmpty()) {
    return {};
  }

  QFileInfo info(path);
  if (info.isFile()) {
    return {info.absoluteFilePath()};
  }
  if (!info.isDir()) {
    return {};
  }

  QDir dir(info.absoluteFilePath());
  QStringList nameFilters;
  for (QString const &suffix : imageSuffixes()) {
    nameFilters << QStringLiteral("*.") + suffix;
    nameFilters << QStringLiteral("*.") + suffix.toUpper();
  }

  QStringList files = dir.entryList(nameFilters, QDir::Files, QDir::NoSort);
  QCollator collator;
  collator.setNumericMode(true);
  collator.setCaseSensitivity(Qt::CaseInsensitive);
  std::sort(files.begin(), files.end(), [&collator](QString const &left, QString const &right) {
    return collator.compare(left, right) < 0;
  });

  QStringList absolute;
  absolute.reserve(files.size());
  for (QString const &file : files) {
    absolute.push_back(dir.absoluteFilePath(file));
  }
  return absolute;
}
