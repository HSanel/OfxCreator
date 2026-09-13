#pragma once

#include <QtNodes/NodeDelegateModel>

#include <QImage>

class OutputNodeModel : public QtNodes::NodeDelegateModel
{
  Q_OBJECT

public:
  OutputNodeModel();

  QString caption() const override { return QStringLiteral("Output"); }
  QString name() const override { return QStringLiteral("Output"); }

  unsigned int nPorts(QtNodes::PortType portType) const override;
  QtNodes::NodeDataType dataType(QtNodes::PortType portType,
                                 QtNodes::PortIndex portIndex) const override;
  std::shared_ptr<QtNodes::NodeData> outData(QtNodes::PortIndex) override { return {}; }
  void setInData(std::shared_ptr<QtNodes::NodeData> data, QtNodes::PortIndex portIndex) override;
  QWidget *embeddedWidget() override;

  QImage currentImage() const { return m_image; }

Q_SIGNALS:
  void imageChanged(QImage image);

private:
  QImage m_image;
  QWidget *m_widget = nullptr;
};
