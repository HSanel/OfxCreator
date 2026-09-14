#pragma once

#include <QString>

enum class NodeKind {
  Input,
  Output,
  Cpu,
  Cuda,
  OpenCl,
  Python,
  Plugin,
  Unknown
};

inline QString nodeKindId(NodeKind kind)
{
  switch (kind) {
  case NodeKind::Input:
    return QStringLiteral("Input");
  case NodeKind::Output:
    return QStringLiteral("Output");
  case NodeKind::Cpu:
    return QStringLiteral("CPU");
  case NodeKind::Cuda:
    return QStringLiteral("CUDA");
  case NodeKind::OpenCl:
    return QStringLiteral("OpenCL");
  case NodeKind::Python:
    return QStringLiteral("Python");
  case NodeKind::Plugin:
    return QStringLiteral("Plugin");
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
  if (id == QLatin1String("CPU")) {
    return NodeKind::Cpu;
  }
  if (id == QLatin1String("CUDA")) {
    return NodeKind::Cuda;
  }
  if (id == QLatin1String("OpenCL")) {
    return NodeKind::OpenCl;
  }
  if (id == QLatin1String("Python")) {
    return NodeKind::Python;
  }
  if (id == QLatin1String("Plugin")) {
    return NodeKind::Plugin;
  }
  return NodeKind::Unknown;
}
