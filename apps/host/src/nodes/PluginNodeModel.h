#pragma once

#include <QtNodes/NodeDelegateModel>

#include <QImage>
#include <QJsonObject>
#include <memory>

class NrOfxLoader;
class PlaybackState;
class QLabel;
class QLineEdit;
class QWidget;

class PluginNodeModel : public QtNodes::NodeDelegateModel
{
  Q_OBJECT

public:
  explicit PluginNodeModel(PlaybackState *playback);
  ~PluginNodeModel() override;

  QString caption() const override { return QStringLiteral("Plugin"); }
  QString name() const override { return QStringLiteral("Plugin"); }

  unsigned int nPorts(QtNodes::PortType portType) const override;
  QtNodes::NodeDataType dataType(QtNodes::PortType portType, QtNodes::PortIndex portIndex) const override;
  QString portCaption(QtNodes::PortType portType, QtNodes::PortIndex portIndex) const override;
  bool portCaptionVisible(QtNodes::PortType, QtNodes::PortIndex) const override { return true; }
  std::shared_ptr<QtNodes::NodeData> outData(QtNodes::PortIndex port) override;
  void setInData(std::shared_ptr<QtNodes::NodeData> data, QtNodes::PortIndex portIndex) override;
  QWidget *embeddedWidget() override;

  QJsonObject save() const override;
  void load(QJsonObject const &json) override;

  bool loadPath(QString const &path, QString *error = nullptr);
  QString bundlePath() const { return m_path; }
  QString pluginId() const;
  QImage currentImage() const { return m_output; }

private:
  void browsePlugin();
  void processCurrent();
  void setStatus(QString const &text, bool ok);
  void refreshPathEdit();
  QWidget *dialogParent() const;

  PlaybackState *m_playback = nullptr;
  std::unique_ptr<NrOfxLoader> m_loader;
  QImage m_input;
  QImage m_output;
  int m_frameIndex = 0;
  QString m_path;

  QWidget *m_widget = nullptr;
  QLineEdit *m_pathEdit = nullptr;
  QLabel *m_status = nullptr;
};
