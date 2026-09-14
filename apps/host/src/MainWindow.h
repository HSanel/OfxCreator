#pragma once

#include <QMainWindow>

#include <QtNodes/Definitions>

#include <memory>

class PlaybackState;
class ViewerPanel;

namespace QtNodes {
class DataFlowGraphModel;
class DataFlowGraphicsScene;
class GraphicsView;
class NodeDelegateModelRegistry;
}

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

private:
  bool eventFilter(QObject *watched, QEvent *event) override;

  void createMenus();
  void registerNodeTypes();
  void bindNode(unsigned int nodeId);
  void seedDefaultGraph();
  void frameGraphView();
  void updateFrameCount();
  void updateWindowTitle();
  bool saveTo(QString const &path);
  void newDocument();
  void openDocument();
  bool saveDocument();
  bool saveDocumentAs();
  void exportOfx();

  void inspectNode(QtNodes::NodeId nodeId);
  void inspectPort(QtNodes::NodeId nodeId, QtNodes::PortType portType, QtNodes::PortIndex portIndex);
  void inspectConnection(QtNodes::ConnectionId const &connectionId);
  void onGraphSelectionChanged();
  void refreshInspectedView();
  QImage imageAtPort(QtNodes::NodeId nodeId,
                     QtNodes::PortType portType,
                     QtNodes::PortIndex portIndex);
  bool inspects(QtNodes::NodeId nodeId) const;

  std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_registry;
  std::unique_ptr<QtNodes::DataFlowGraphModel> m_graphModel;
  QtNodes::DataFlowGraphicsScene *m_scene = nullptr;
  QtNodes::GraphicsView *m_graphView = nullptr;
  PlaybackState *m_playback = nullptr;
  ViewerPanel *m_viewer = nullptr;
  QString m_documentPath;

  enum class InspectKind { None, Node, Port, Connection };
  InspectKind m_inspectKind = InspectKind::None;
  QtNodes::NodeId m_inspectNodeId = QtNodes::InvalidNodeId;
  QtNodes::PortType m_inspectPortType = QtNodes::PortType::Out;
  QtNodes::PortIndex m_inspectPortIndex = 0;
  QtNodes::ConnectionId m_inspectConnection{};
  bool m_skipNodeSelect = false;
};
