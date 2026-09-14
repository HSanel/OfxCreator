#pragma once

#include "io/ImageSequence.h"

#include <QtNodes/NodeDelegateModel>

class PlaybackState;
class QLineEdit;
class QWidget;

class InputNodeModel : public QtNodes::NodeDelegateModel
{
  Q_OBJECT

public:
  explicit InputNodeModel(PlaybackState *playback);

  QString caption() const override { return QStringLiteral("Input"); }
  QString name() const override { return QStringLiteral("Input"); }

  unsigned int nPorts(QtNodes::PortType portType) const override;
  QtNodes::NodeDataType dataType(QtNodes::PortType portType,
                                 QtNodes::PortIndex portIndex) const override;
  std::shared_ptr<QtNodes::NodeData> outData(QtNodes::PortIndex port) override;
  void setInData(std::shared_ptr<QtNodes::NodeData>, QtNodes::PortIndex) override {}
  QWidget *embeddedWidget() override;

  QJsonObject save() const override;
  void load(QJsonObject const &json) override;

  QString sequencePath() const { return m_sourcePath; }
  int frameCount() const { return m_sequence.frameCount(); }
  QImage currentImage() const;
  void setSequencePath(QString const &path);

Q_SIGNALS:
  void sequenceChanged();

private:
  void browseFile();
  void browseFolder();
  void refreshPathEdit();
  void emitCurrentFrame();
  QWidget *dialogParent() const;

  PlaybackState *m_playback = nullptr;
  ImageSequence m_sequence;
  QString m_sourcePath;
  QWidget *m_widget = nullptr;
  QLineEdit *m_pathEdit = nullptr;
};
