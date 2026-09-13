#pragma once

#include "ir/NodeKind.h"

#include <QtNodes/NodeDelegateModel>

#include <QImage>
#include <QJsonObject>
#include <QVector>
#include <memory>

class KernelRuntime;
class QLabel;
class QPlainTextEdit;
class QLineEdit;
class QFileSystemWatcher;
class QTimer;
class QWidget;

class KernelNodeModel : public QtNodes::NodeDelegateModel
{
  Q_OBJECT

public:
  explicit KernelNodeModel(NodeKind kind);
  ~KernelNodeModel() override;

  QString caption() const override;
  QString name() const override;
  bool resizable() const override { return true; }

  unsigned int nPorts(QtNodes::PortType portType) const override;
  QtNodes::NodeDataType dataType(QtNodes::PortType portType, QtNodes::PortIndex portIndex) const override;
  QString portCaption(QtNodes::PortType portType, QtNodes::PortIndex portIndex) const override;
  bool portCaptionVisible(QtNodes::PortType, QtNodes::PortIndex) const override { return true; }
  std::shared_ptr<QtNodes::NodeData> outData(QtNodes::PortIndex port) override;
  void setInData(std::shared_ptr<QtNodes::NodeData> data, QtNodes::PortIndex portIndex) override;
  QWidget *embeddedWidget() override;

  QJsonObject save() const override;
  void load(QJsonObject const &json) override;

  QString source() const;
  QString filePath() const;
  QImage currentImage() const { return m_outputs.isEmpty() ? QImage() : m_outputs.front(); }

private:
  void browseSource();
  void loadKernel();
  void compileSource();
  void onFileChanged();
  void setStatus(QString const &text, bool ok);
  void processCurrent();
  void syncPortsFromSource();
  QWidget *dialogParent() const;
  QString fileFilter() const;

  NodeKind m_kind = NodeKind::Unknown;
  std::unique_ptr<KernelRuntime> m_runtime;
  QVector<QImage> m_inputs;
  QVector<QImage> m_outputs;
  int m_inCount = 1;
  int m_outCount = 1;
  int m_frameIndex = 0;
  QString m_filePath;
  bool m_loaded = false;

  QWidget *m_widget = nullptr;
  QLineEdit *m_pathEdit = nullptr;
  QPlainTextEdit *m_editor = nullptr;
  QLabel *m_status = nullptr;
  QFileSystemWatcher *m_watcher = nullptr;
  QTimer *m_reloadTimer = nullptr;
  QTimer *m_portSyncTimer = nullptr;
};

class CpuNodeModel : public KernelNodeModel
{
public:
  CpuNodeModel()
    : KernelNodeModel(NodeKind::Cpu)
  {
  }
};

class CudaNodeModel : public KernelNodeModel
{
public:
  CudaNodeModel()
    : KernelNodeModel(NodeKind::Cuda)
  {
  }
};

class OpenClNodeModel : public KernelNodeModel
{
public:
  OpenClNodeModel()
    : KernelNodeModel(NodeKind::OpenCl)
  {
  }
};

class PythonNodeModel : public KernelNodeModel
{
public:
  PythonNodeModel()
    : KernelNodeModel(NodeKind::Python)
  {
  }
};
