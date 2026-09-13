#include "VideoIngest.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

namespace {

QStringList const &videoSuffixes()
{
  static QStringList const suffixes{QStringLiteral("mp4"),
                                    QStringLiteral("mov"),
                                    QStringLiteral("mkv"),
                                    QStringLiteral("avi"),
                                    QStringLiteral("webm"),
                                    QStringLiteral("m4v"),
                                    QStringLiteral("mpg"),
                                    QStringLiteral("mpeg"),
                                    QStringLiteral("wmv")};
  return suffixes;
}

QString cacheRoot()
{
  QString const base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  QDir dir(base.isEmpty() ? QDir::tempPath() : base);
  dir.mkpath(QStringLiteral("ofx-host/ffmpeg"));
  return dir.filePath(QStringLiteral("ofx-host/ffmpeg"));
}

QByteArray sourceFingerprint(QFileInfo const &info)
{
  QByteArray payload;
  payload += info.canonicalFilePath().toUtf8();
  payload += '\n';
  payload += QByteArray::number(info.size());
  payload += '\n';
  payload += QByteArray::number(info.lastModified().toMSecsSinceEpoch());
  return QCryptographicHash::hash(payload, QCryptographicHash::Sha1).toHex();
}

bool cacheIsValid(QDir const &dir, QFileInfo const &source)
{
  QFile metaFile(dir.filePath(QStringLiteral("source.json")));
  if (!metaFile.open(QIODevice::ReadOnly)) {
    return false;
  }
  QJsonObject const meta = QJsonDocument::fromJson(metaFile.readAll()).object();
  return meta.value(QStringLiteral("path")).toString() == source.canonicalFilePath()
         && meta.value(QStringLiteral("size")).toInteger() == source.size()
         && meta.value(QStringLiteral("mtime")).toInteger() == source.lastModified().toMSecsSinceEpoch()
         && !dir.entryList({QStringLiteral("frame_*.png")}, QDir::Files).isEmpty();
}

bool writeMeta(QDir const &dir, QFileInfo const &source)
{
  QJsonObject meta;
  meta.insert(QStringLiteral("path"), source.canonicalFilePath());
  meta.insert(QStringLiteral("size"), source.size());
  meta.insert(QStringLiteral("mtime"), source.lastModified().toMSecsSinceEpoch());
  QFile metaFile(dir.filePath(QStringLiteral("source.json")));
  if (!metaFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  metaFile.write(QJsonDocument(meta).toJson(QJsonDocument::Compact));
  return true;
}

} // namespace

bool VideoIngest::isVideoFile(QString const &path)
{
  QString const suffix = QFileInfo(path).suffix().toLower();
  return videoSuffixes().contains(suffix);
}

QString VideoIngest::ffmpegExecutable()
{
  if (QByteArray const env = qgetenv("FFMPEG"); !env.isEmpty()) {
    QString const fromEnv = QString::fromLocal8Bit(env);
    if (QFileInfo::exists(fromEnv)) {
      return fromEnv;
    }
  }

  if (!QCoreApplication::applicationDirPath().isEmpty()) {
    QString const besideApp =
      QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ffmpeg.exe"));
    if (QFileInfo::exists(besideApp)) {
      return besideApp;
    }
  }

  QString found = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (!found.isEmpty()) {
    return found;
  }
  found = QStandardPaths::findExecutable(QStringLiteral("ffmpeg.exe"));
  if (!found.isEmpty()) {
    return found;
  }

  QStringList hints{QStringLiteral("C:/ffmpeg/bin/ffmpeg.exe"),
                    QStringLiteral("C:/Program Files/ffmpeg/bin/ffmpeg.exe"),
                    QStringLiteral("C:/Program Files/FFmpeg/bin/ffmpeg.exe")};
  QString const sourceTree = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("../../third_party/ffmpeg/bin/ffmpeg.exe"));
  hints.prepend(QFileInfo(sourceTree).absoluteFilePath());

  for (QString const &hint : hints) {
    if (QFileInfo::exists(hint)) {
      return hint;
    }
  }
  return {};
}

QString VideoIngest::ensureSequence(QString const &videoPath, QString *error)
{
  QFileInfo const source(videoPath);
  if (!source.isFile()) {
    if (error) {
      *error = QStringLiteral("Videodatei nicht gefunden.");
    }
    return {};
  }

  QString const ffmpeg = ffmpegExecutable();
  if (ffmpeg.isEmpty()) {
    if (error) {
      *error = QStringLiteral("ffmpeg wurde nicht gefunden. Bitte ffmpeg in den PATH legen.");
    }
    return {};
  }

  QDir dir(QDir(cacheRoot()).filePath(QString::fromLatin1(sourceFingerprint(source))));
  if (cacheIsValid(dir, source)) {
    return dir.absolutePath();
  }

  if (dir.exists()) {
    dir.removeRecursively();
  }
  QDir().mkpath(dir.absolutePath());

  QString const pattern = dir.filePath(QStringLiteral("frame_%06d.png"));
  QProcess process;
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start(ffmpeg,
                QStringList{QStringLiteral("-hide_banner"),
                            QStringLiteral("-nostdin"),
                            QStringLiteral("-y"),
                            QStringLiteral("-i"),
                            source.absoluteFilePath(),
                            QStringLiteral("-start_number"),
                            QStringLiteral("0"),
                            pattern});
  if (!process.waitForStarted(8000)) {
    if (error) {
      *error = QStringLiteral("ffmpeg konnte nicht gestartet werden.");
    }
    return {};
  }
  if (!process.waitForFinished(-1) || process.exitStatus() != QProcess::NormalExit
      || process.exitCode() != 0) {
    if (error) {
      QString const log = QString::fromLocal8Bit(process.readAll());
      *error = QStringLiteral("ffmpeg-Fehler:\n%1").arg(log.left(800));
    }
    return {};
  }

  if (dir.entryList({QStringLiteral("frame_*.png")}, QDir::Files).isEmpty()) {
    if (error) {
      *error = QStringLiteral("ffmpeg hat keine Frames geschrieben.");
    }
    return {};
  }

  writeMeta(dir, source);
  return dir.absolutePath();
}
