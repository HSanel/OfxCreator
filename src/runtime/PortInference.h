#pragma once

#include "ir/NodeKind.h"

#include <QString>

struct KernelPortLayout
{
  int inCount = 1;
  int outCount = 1;
};

KernelPortLayout inferKernelPorts(NodeKind kind, QString const &source);
