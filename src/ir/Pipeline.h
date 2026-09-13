#pragma once

#include "NodeKind.h"

#include <QJsonObject>
#include <QVector>
#include <QtGlobal>

struct PipelineNode
{
  quint64 id = 0;
  NodeKind kind = NodeKind::Unknown;
  double x = 0.0;
  double y = 0.0;
  QJsonObject params;
};

struct PipelineConnection
{
  quint64 fromId = 0;
  int fromPort = 0;
  quint64 toId = 0;
  int toPort = 0;
};

class Pipeline
{
public:
  static constexpr int kFormatVersion = 1;
  static constexpr const char *kFormatId = "nr-ofx-pipeline";

  int currentFrame = 0;
  QVector<PipelineNode> nodes;
  QVector<PipelineConnection> connections;

  QJsonObject toJson() const;
  static Pipeline fromJson(QJsonObject const &json, QString *error = nullptr);
};
