#include "OutputNodeModel.h"

#include "ImageData.h"

#include <QLabel>
#include <QVBoxLayout>

OutputNodeModel::OutputNodeModel() = default;

unsigned int OutputNodeModel::nPorts(QtNodes::PortType portType) const
{
  return portType == QtNodes::PortType::In ? 1 : 0;
}

QtNodes::NodeDataType OutputNodeModel::dataType(QtNodes::PortType, QtNodes::PortIndex) const
{
  return ImageData().type();
}

void OutputNodeModel::setInData(std::shared_ptr<QtNodes::NodeData> data, QtNodes::PortIndex)
{
  auto imageData = std::dynamic_pointer_cast<ImageData>(data);
  if (!imageData || imageData->isNull()) {
    m_image = QImage();
    Q_EMIT imageChanged(m_image);
    return;
  }
  m_image = imageData->image();
  Q_EMIT imageChanged(m_image);
}

QWidget *OutputNodeModel::embeddedWidget()
{
  if (m_widget) {
    return m_widget;
  }
  m_widget = new QWidget;
  auto *layout = new QVBoxLayout(m_widget);
  layout->setContentsMargins(8, 8, 8, 8);
  auto *label = new QLabel(QStringLiteral("Viewer-Ziel"));
  label->setAlignment(Qt::AlignCenter);
  layout->addWidget(label);
  return m_widget;
}
