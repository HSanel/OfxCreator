#pragma once

#include <QString>

enum class NodeKind {
  Input,
  Output,
  Unknown
};

inline QString nodeKindId(NodeKind kind)
{
  switch (kind) {
  case NodeKind::Input:
    return QStringLiteral("Input");
  case NodeKind::Output:
    return QStringLiteral("Output");
  case NodeKind::Unknown:
    break;
  }
  return QStringLiteral("Unknown");
}

inline NodeKind nodeKindFromId(QString const &id)
{
  if (id == QLatin1String("Input")) {
    return NodeKind::Input;
  }
  if (id == QLatin1String("Output")) {
    return NodeKind::Output;
  }
  return NodeKind::Unknown;
}
