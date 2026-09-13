#pragma once

#include <QString>

class VideoIngest
{
public:
  static bool isVideoFile(QString const &path);
  static QString ffmpegExecutable();

  // Returns a directory of extracted PNG frames, or empty on failure.
  static QString ensureSequence(QString const &videoPath, QString *error);
};
