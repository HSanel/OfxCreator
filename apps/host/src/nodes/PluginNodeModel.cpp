#include "PluginNodeModel.h"

#include "ImageData.h"
#include "PlaybackState.h"
#include "ofx/NrOfxLoader.h"

#include <algorithm>
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

PluginNodeModel::PluginNodeModel(PlaybackState *playback)
  : m_playback(playback)
  , m_loader(std::make_unique<NrOfxLoader>())
{
}

PluginNodeModel::~PluginNodeModel() = default;

unsigned int PluginNodeModel::nPorts(QtNodes::PortType) const
{
  return 1;
}

QtNodes::NodeDataType PluginNodeModel::dataType(QtNodes::PortType, QtNodes::PortIndex) const
{
  return ImageData().type();
}

QString PluginNodeModel::portCaption(QtNodes::PortType portType, QtNodes::PortIndex) const
{
  return portType == QtNodes::PortType::In ? QStringLiteral("Source") : QStringLiteral("Output");
}

std::shared_ptr<QtNodes::NodeData> PluginNodeModel::outData(QtNodes::PortIndex)
{
  if (m_output.isNull()) {
    return {};
  }
  return std::make_shared<ImageData>(m_output, m_frameIndex);
}

void PluginNodeModel::setInData(std::shared_ptr<QtNodes::NodeData> data, QtNodes::PortIndex)
{
  auto imageData = std::dynamic_pointer_cast<ImageData>(data);
  if (!imageData || imageData->isNull()) {
    m_input = {};
    m_output = {};
    Q_EMIT dataInvalidated(0);
    return;
  }
  m_input = imageData->image();
  m_frameIndex = imageData->frameIndex();
  processCurrent();
}

QWidget *PluginNodeModel::embeddedWidget()
{
  if (m_widget) {
    return m_widget;
  }

  m_widget = new QWidget;
  m_widget->setMinimumWidth(220);
  auto *layout = new QVBoxLayout(m_widget);
  layout->setContentsMargins(4, 4, 4, 4);

  auto *pathRow = new QWidget;
  auto *pathLayout = new QHBoxLayout(pathRow);
  pathLayout->setContentsMargins(0, 0, 0, 0);
  m_pathEdit = new QLineEdit;
  m_pathEdit->setReadOnly(true);
  m_pathEdit->setPlaceholderText(QStringLiteral("*.ofx / .ofx.bundle"));
  auto *browse = new QPushButton(QStringLiteral("Laden…"));
  pathLayout->addWidget(m_pathEdit, 1);
  pathLayout->addWidget(browse);

  m_status = new QLabel(QStringLiteral("Kein Plugin geladen."));
  m_status->setWordWrap(true);

  layout->addWidget(pathRow);
  layout->addWidget(m_status);

  connect(browse, &QPushButton::clicked, this, &PluginNodeModel::browsePlugin);
  refreshPathEdit();
  if (m_loader->isLoaded()) {
    setStatus(QStringLiteral("OFX: %1").arg(m_loader->pluginId()), true);
  }
  return m_widget;
}

QJsonObject PluginNodeModel::save() const
{
  QJsonObject json = NodeDelegateModel::save();
  json.insert(QStringLiteral("path"), m_path);
  return json;
}

void PluginNodeModel::load(QJsonObject const &json)
{
  QString const path = json.value(QStringLiteral("path")).toString();
  if (!path.isEmpty()) {
    loadPath(path);
  }
}

bool PluginNodeModel::loadPath(QString const &path, QString *error)
{
  QString localError;
  QString *err = error ? error : &localError;
  if (!m_loader->loadBundle(path, err)) {
    m_path = path;
    refreshPathEdit();
    setStatus(*err, false);
    m_output = {};
    Q_EMIT dataInvalidated(0);
    return false;
  }
  m_path = path;
  refreshPathEdit();
  setStatus(QStringLiteral("OFX: %1").arg(m_loader->pluginId()), true);
  processCurrent();
  return true;
}

QString PluginNodeModel::pluginId() const
{
  return m_loader ? m_loader->pluginId() : QString();
}

void PluginNodeModel::browsePlugin()
{
  QString const path = QFileDialog::getOpenFileName(dialogParent(),
                                                    QStringLiteral("OFX Plugin laden"),
                                                    m_path,
                                                    QStringLiteral("OFX Plugin (*.ofx);;Alle Dateien (*)"));
  if (path.isEmpty()) {
    return;
  }
  QString error;
  if (!loadPath(path, &error)) {
    QMessageBox::warning(dialogParent(), QStringLiteral("OFX laden"), error);
  }
}

void PluginNodeModel::processCurrent()
{
  if (!m_loader->isLoaded()) {
    m_output = {};
    Q_EMIT dataInvalidated(0);
    return;
  }
  if (m_input.isNull()) {
    m_output = {};
    setStatus(QStringLiteral("OFX: %1 — kein Source-Bild.").arg(m_loader->pluginId()), false);
    Q_EMIT dataInvalidated(0);
    return;
  }

  QString error;
  int const frame = m_playback ? m_playback->frame() : m_frameIndex;
  int const count = m_playback ? std::max(1, m_playback->frameCount()) : 1;
  m_output = m_loader->render(m_input, frame, count, &error);
  if (m_output.isNull()) {
    setStatus(error.isEmpty() ? QStringLiteral("Render fehlgeschlagen.") : error, false);
    Q_EMIT dataInvalidated(0);
    return;
  }
  setStatus(QStringLiteral("OFX: %1").arg(m_loader->pluginId()), true);
  Q_EMIT dataUpdated(0);
}

void PluginNodeModel::setStatus(QString const &text, bool ok)
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

void PluginNodeModel::refreshPathEdit()
{
  if (!m_pathEdit) {
    return;
  }
  m_pathEdit->setText(m_path);
  m_pathEdit->setToolTip(m_path);
}

QWidget *PluginNodeModel::dialogParent() const
{
  if (QWidget *active = QApplication::activeWindow()) {
    if (qobject_cast<QMainWindow *>(active)) {
      return active;
    }
  }
  for (QWidget *widget : QApplication::topLevelWidgets()) {
    if (auto *mainWindow = qobject_cast<QMainWindow *>(widget)) {
      if (mainWindow->isVisible()) {
        return mainWindow;
      }
    }
  }
  return nullptr;
}
