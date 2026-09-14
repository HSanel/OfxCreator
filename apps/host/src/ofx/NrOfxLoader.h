#pragma once

#include <QImage>
#include <QString>

#include <memory>

class NrOfxLoader
{
public:
  NrOfxLoader();
  ~NrOfxLoader();

  bool loadBundle(QString const &path, QString *error);
  void unload();
  bool isLoaded() const;
  QString pluginId() const { return m_pluginId; }
  QImage render(QImage const &source, int frame, int frameCount, QString *error);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
  QString m_pluginId;
  bool m_countsLoaded = false;
};
