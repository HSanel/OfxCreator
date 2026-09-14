#pragma once

#include "ir/Pipeline.h"

#include <QString>
#include <QStringList>

struct OfxExportResult
{
  bool ok = false;
  QString projectDir;
  QString bundlePath;
  QString pluginPath;
  QStringList warnings;
  QString error;
};

class OfxExporter
{
public:
  static OfxExportResult exportPipeline(Pipeline const &pipeline, QString const &projectDir, QString const &pluginName);
};
