#pragma once

#include "RgbaImage.h"

#include <QString>
#include <QVector>
#include <memory>

class KernelRuntime
{
public:
  virtual ~KernelRuntime() = default;
  virtual bool load(QString const &source, QString *error) = 0;
  virtual bool process(QVector<RgbaImage> const &inputs, QVector<RgbaImage> *outputs, QString *error) = 0;
};

std::unique_ptr<KernelRuntime> createCpuRuntime();
std::unique_ptr<KernelRuntime> createCudaRuntime();
std::unique_ptr<KernelRuntime> createOpenClRuntime();
std::unique_ptr<KernelRuntime> createPythonRuntime();
