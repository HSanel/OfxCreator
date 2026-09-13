#include "KernelNodeModel.h"

#include "ImageData.h"
#include "runtime/DefaultSources.h"
#include "runtime/KernelRuntime.h"
#include "runtime/PortInference.h"
#include "runtime/RgbaImage.h"

#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

std::unique_ptr<KernelRuntime> makeRuntime(NodeKind kind)
{
  switch (kind) {
  case NodeKind::Cpu:
    return createCpuRuntime();
  case NodeKind::Cuda:
    return createCudaRuntime();
  case NodeKind::OpenCl:
    return createOpenClRuntime();
  case NodeKind::Python:
    return createPythonRuntime();
  default:
    break;
  }
  return {};
}

} // namespace

KernelNodeModel::KernelNodeModel(NodeKind kind)
  : m_kind(kind)
  , m_runtime(makeRuntime(kind))
{
  m_inputs.resize(1);
  m_outputs.resize(1);
}

KernelNodeModel::~KernelNodeModel() = default;

QString KernelNodeModel::caption() const
{
  return nodeKindId(m_kind);
}

QString KernelNodeModel::name() const
{
  return nodeKindId(m_kind);
}

unsigned int KernelNodeModel::nPorts(QtNodes::PortType portType) const
{
  return portType == QtNodes::PortType::In ? unsigned(m_inCount) : unsigned(m_outCount);
}

QString KernelNodeModel::portCaption(QtNodes::PortType portType, QtNodes::PortIndex portIndex) const
{
  QString const side = portType == QtNodes::PortType::In ? QStringLiteral("In") : QStringLiteral("Out");
  return QStringLiteral("%1 %2").arg(side).arg(portIndex);
}

QtNodes::NodeDataType KernelNodeModel::dataType(QtNodes::PortType, QtNodes::PortIndex) const
{
  return ImageData().type();
}

std::shared_ptr<QtNodes::NodeData> KernelNodeModel::outData(QtNodes::PortIndex port)
{
  if (port < 0 || port >= m_outputs.size() || m_outputs[port].isNull()) {
    return {};
  }
  return std::make_shared<ImageData>(m_outputs[port], m_frameIndex);
}

void KernelNodeModel::setInData(std::shared_ptr<QtNodes::NodeData> data, QtNodes::PortIndex portIndex)
{
  if (portIndex < 0) {
    return;
  }
  if (portIndex >= m_inputs.size()) {
    m_inputs.resize(portIndex + 1);
  }
  auto imageData = std::dynamic_pointer_cast<ImageData>(data);
  if (!imageData || imageData->isNull()) {
    m_inputs[portIndex] = {};
    processCurrent();
    return;
  }
  m_inputs[portIndex] = imageData->image();
  m_frameIndex = imageData->frameIndex();
  processCurrent();
}

QWidget *KernelNodeModel::embeddedWidget()
{
  if (m_widget) {
    return m_widget;
  }

  m_widget = new QWidget;
  m_widget->setMinimumSize(220, 140);
  m_widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  auto *layout = new QVBoxLayout(m_widget);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(4);

  auto *pathRow = new QWidget;
  auto *pathLayout = new QHBoxLayout(pathRow);
  pathLayout->setContentsMargins(0, 0, 0, 0);
  m_pathEdit = new QLineEdit;
  m_pathEdit->setReadOnly(true);
  m_pathEdit->setPlaceholderText(QStringLiteral("Kernel-Datei (optional)"));
  auto *browse = new QPushButton(QStringLiteral("Datei…"));
  auto *load = new QPushButton(QStringLiteral("Laden"));
  pathLayout->addWidget(m_pathEdit, 1);
  pathLayout->addWidget(browse);
  pathLayout->addWidget(load);

  pathRow->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

  m_editor = new QPlainTextEdit;
  m_editor->setPlainText(defaultKernelSource(m_kind));
  m_editor->setMinimumHeight(80);
  m_editor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
  m_editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_editor->setTabStopDistance(16);
  m_editor->setFocusPolicy(Qt::StrongFocus);
  m_editor->setAttribute(Qt::WA_OpaquePaintEvent, false);

  m_status = new QLabel(QStringLiteral("Kernel noch nicht geladen."));
  m_status->setWordWrap(true);
  m_status->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  m_status->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

  layout->addWidget(pathRow);
  layout->addWidget(m_editor, 1);
  layout->addWidget(m_status);

  m_watcher = new QFileSystemWatcher(m_widget);
  m_reloadTimer = new QTimer(m_widget);
  m_reloadTimer->setSingleShot(true);
  m_reloadTimer->setInterval(200);
  m_portSyncTimer = new QTimer(m_widget);
  m_portSyncTimer->setSingleShot(true);
  m_portSyncTimer->setInterval(350);

  connect(browse, &QPushButton::clicked, this, &KernelNodeModel::browseSource);
  connect(load, &QPushButton::clicked, this, &KernelNodeModel::loadKernel);
  connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this](QString const &) {
    m_reloadTimer->start();
  });
  connect(m_reloadTimer, &QTimer::timeout, this, &KernelNodeModel::onFileChanged);
  connect(m_editor, &QPlainTextEdit::textChanged, this, [this]() { m_portSyncTimer->start(); });
  connect(m_portSyncTimer, &QTimer::timeout, this, &KernelNodeModel::syncPortsFromSource);

  syncPortsFromSource();
  return m_widget;
}

QJsonObject KernelNodeModel::save() const
{
  QJsonObject json = NodeDelegateModel::save();
  json.insert(QStringLiteral("file"), m_filePath);
  json.insert(QStringLiteral("source"), source());
  if (m_widget) {
    json.insert(QStringLiteral("width"), m_widget->width());
    json.insert(QStringLiteral("height"), m_widget->height());
  }
  return json;
}

void KernelNodeModel::compileSource()
{
  syncPortsFromSource();
  if (!m_runtime) {
    setStatus(QStringLiteral("Runtime fehlt."), false);
    return;
  }
  QString error;
  m_loaded = m_runtime->load(source(), &error);
  if (!m_loaded) {
    setStatus(error, false);
    processCurrent();
    return;
  }
  setStatus(QStringLiteral("Kernel geladen · %1 In / %2 Out").arg(m_inCount).arg(m_outCount), true);
  processCurrent();
}

void KernelNodeModel::load(QJsonObject const &json)
{
  NodeDelegateModel::load(json);
  embeddedWidget();
  m_filePath = json.value(QStringLiteral("file")).toString();
  QString const source = json.value(QStringLiteral("source")).toString();
  if (m_pathEdit) {
    m_pathEdit->setText(m_filePath);
  }
  if (m_editor) {
    m_editor->setPlainText(source.isEmpty() ? defaultKernelSource(m_kind) : source);
  }
  int const width = json.value(QStringLiteral("width")).toInt();
  int const height = json.value(QStringLiteral("height")).toInt();
  if (m_widget && width > 0 && height > 0) {
    m_widget->setFixedSize(std::max(220, width), std::max(140, height));
    Q_EMIT embeddedWidgetSizeUpdated();
    Q_EMIT requestNodeUpdate();
  }
  if (m_watcher && !m_filePath.isEmpty()) {
    m_watcher->addPath(m_filePath);
  }
  compileSource();
}

QString KernelNodeModel::source() const
{
  return m_editor ? m_editor->toPlainText() : defaultKernelSource(m_kind);
}

QString KernelNodeModel::filePath() const
{
  return m_filePath;
}

void KernelNodeModel::browseSource()
{
  QTimer::singleShot(0, this, [this]() {
    QString const file = QFileDialog::getOpenFileName(dialogParent(),
                                                      QStringLiteral("Kernel wählen"),
                                                      m_filePath,
                                                      fileFilter());
    if (file.isEmpty()) {
      return;
    }
    m_filePath = file;
    if (m_pathEdit) {
      m_pathEdit->setText(m_filePath);
    }
    if (m_watcher) {
      if (!m_watcher->files().isEmpty()) {
        m_watcher->removePaths(m_watcher->files());
      }
      m_watcher->addPath(m_filePath);
    }
    onFileChanged();
  });
}

void KernelNodeModel::loadKernel()
{
  if (m_editor && !m_filePath.isEmpty()) {
    QFile file(m_filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
      file.write(m_editor->toPlainText().toUtf8());
    }
  }
  compileSource();
}

void KernelNodeModel::onFileChanged()
{
  if (m_filePath.isEmpty()) {
    return;
  }
  QFile file(m_filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    setStatus(QStringLiteral("Kernel-Datei nicht lesbar."), false);
    return;
  }
  QString const text = QString::fromUtf8(file.readAll());
  if (m_editor) {
    m_editor->setPlainText(text);
  }
  if (m_watcher && !m_watcher->files().contains(m_filePath)) {
    m_watcher->addPath(m_filePath);
  }
  compileSource();
}

void KernelNodeModel::setStatus(QString const &text, bool ok)
{
  if (!m_status) {
    embeddedWidget();
  }
  if (!m_status) {
    return;
  }
  m_status->setText(text);
  m_status->setStyleSheet(ok ? QStringLiteral("color: #8f8;") : QStringLiteral("color: #f88;"));
}

void KernelNodeModel::processCurrent()
{
  bool missing = false;
  QVector<RgbaImage> ins;
  ins.reserve(m_inCount);
  for (int i = 0; i < m_inCount; ++i) {
    if (i >= m_inputs.size() || m_inputs[i].isNull()) {
      missing = true;
      break;
    }
    ins.push_back(rgbaFromQImage(m_inputs[i]));
  }
  if (missing) {
    m_outputs.fill(QImage(), m_outCount);
    for (int i = 0; i < m_outCount; ++i) {
      Q_EMIT dataUpdated(i);
    }
    return;
  }
  if (!m_loaded || !m_runtime) {
    m_outputs.resize(m_outCount);
    for (int i = 0; i < m_outCount; ++i) {
      m_outputs[i] = m_inputs[0];
      Q_EMIT dataUpdated(i);
    }
    return;
  }

  QVector<RgbaImage> outs;
  QString error;
  if (!m_runtime->process(ins, &outs, &error)) {
    setStatus(error, false);
    m_outputs.resize(m_outCount);
    for (int i = 0; i < m_outCount; ++i) {
      m_outputs[i] = m_inputs[0];
      Q_EMIT dataUpdated(i);
    }
    return;
  }
  m_outputs.resize(m_outCount);
  for (int i = 0; i < m_outCount; ++i) {
    m_outputs[i] = i < outs.size() ? qImageFromRgba(outs[i]) : QImage();
    Q_EMIT dataUpdated(i);
  }
}

void KernelNodeModel::syncPortsFromSource()
{
  KernelPortLayout const layout = inferKernelPorts(m_kind, source());
  auto applyCount = [this](QtNodes::PortType type, int oldCount, int newCount) {
    if (newCount == oldCount) {
      return;
    }
    if (newCount < oldCount) {
      Q_EMIT portsAboutToBeDeleted(type, QtNodes::PortIndex(newCount), QtNodes::PortIndex(oldCount - 1));
      Q_EMIT portsDeleted();
    } else {
      Q_EMIT portsAboutToBeInserted(type, QtNodes::PortIndex(oldCount), QtNodes::PortIndex(newCount - 1));
      Q_EMIT portsInserted();
    }
  };
  applyCount(QtNodes::PortType::In, m_inCount, layout.inCount);
  applyCount(QtNodes::PortType::Out, m_outCount, layout.outCount);
  m_inCount = layout.inCount;
  m_outCount = layout.outCount;
  m_inputs.resize(m_inCount);
  m_outputs.resize(m_outCount);
  Q_EMIT requestNodeUpdate();
}

QWidget *KernelNodeModel::dialogParent() const
{
  if (auto *window = QApplication::activeWindow()) {
    return window;
  }
  for (QWidget *widget : QApplication::topLevelWidgets()) {
    if (qobject_cast<QMainWindow *>(widget)) {
      return widget;
    }
  }
  return m_widget;
}

QString KernelNodeModel::fileFilter() const
{
  switch (m_kind) {
  case NodeKind::Cpu:
    return QStringLiteral("C/C++ (*.cpp *.cc *.c *.h);;Alle Dateien (*.*)");
  case NodeKind::Cuda:
    return QStringLiteral("CUDA (*.cu *.cuh);;Alle Dateien (*.*)");
  case NodeKind::OpenCl:
    return QStringLiteral("OpenCL (*.cl);;Alle Dateien (*.*)");
  case NodeKind::Python:
    return QStringLiteral("Python (*.py);;Alle Dateien (*.*)");
  default:
    break;
  }
  return QStringLiteral("Alle Dateien (*.*)");
}
