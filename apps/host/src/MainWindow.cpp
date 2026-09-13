#include "HostGraphicsView.h"
#include "MainWindow.h"

#include "OfxSdkProbe.h"
#include "PipelineCodec.h"
#include "PlaybackState.h"
#include "ViewerPanel.h"
#include "nodes/ImageData.h"
#include "nodes/InputNodeModel.h"
#include "nodes/OutputNodeModel.h"

#include <QtNodes/BasicGraphicsScene>
#include <QtNodes/ConnectionIdUtils>
#include <QtNodes/DataFlowGraphicsScene>
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/Definitions>
#include <QtNodes/GraphicsView>
#include <QtNodes/NodeData>
#include <QtNodes/NodeDelegateModel>
#include <QtNodes/NodeDelegateModelRegistry>
#include <QtNodes/internal/AbstractNodeGeometry.hpp>
#include <QtNodes/internal/ConnectionGraphicsObject.hpp>
#include <QtNodes/internal/NodeGraphicsObject.hpp>

#include <QAction>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QJsonDocument>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>

#include <algorithm>
#include <stdexcept>

MainWindow::MainWindow(QWidget *parent)
  : QMainWindow(parent)
  , m_registry(std::make_shared<QtNodes::NodeDelegateModelRegistry>())
  , m_playback(new PlaybackState(this))
{
  m_graphModel = std::make_unique<QtNodes::DataFlowGraphModel>(m_registry);

  setWindowTitle(QStringLiteral("NR OFX Host"));
  resize(1280, 720);

  registerNodeTypes();
  createMenus();

  auto *splitter = new QSplitter(Qt::Horizontal, this);
  m_scene = new QtNodes::DataFlowGraphicsScene(*m_graphModel, splitter);
  m_graphView = new HostGraphicsView(m_scene);
  m_graphView->setScene(m_scene);
  m_graphView->setCacheMode(QGraphicsView::CacheNone);
  m_graphView->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  m_graphView->setMinimumSize(240, 240);

  m_viewer = new ViewerPanel;
  splitter->addWidget(m_graphView);
  splitter->addWidget(m_viewer);
  splitter->setChildrenCollapsible(false);
  splitter->setStretchFactor(0, 3);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({900, 380});
  setCentralWidget(splitter);

  connect(m_graphModel.get(),
          &QtNodes::DataFlowGraphModel::nodeCreated,
          this,
          [this](QtNodes::NodeId const nodeId) { bindNode(nodeId); });
  connect(m_graphModel.get(),
          &QtNodes::DataFlowGraphModel::inPortDataWasSet,
          this,
          [this](QtNodes::NodeId const nodeId, QtNodes::PortType, QtNodes::PortIndex) {
            if (inspects(nodeId)) {
              refreshInspectedView();
            }
          });

  m_scene->installEventFilter(this);
  connect(m_scene, &QtNodes::BasicGraphicsScene::nodeSelected, this, [this](QtNodes::NodeId const nodeId) {
    if (m_skipNodeSelect) {
      m_skipNodeSelect = false;
      return;
    }
    inspectNode(nodeId);
  });
  connect(m_scene, &QGraphicsScene::selectionChanged, this, &MainWindow::onGraphSelectionChanged);

  connect(m_playback, &PlaybackState::frameChanged, this, [this](int frame) {
    m_viewer->setFrame(frame);
    refreshInspectedView();
  });
  connect(m_viewer, &ViewerPanel::frameMoved, m_playback, &PlaybackState::setFrame);

  seedDefaultGraph();
  statusBar()->showMessage(ofxSdkVersionLabel());
  updateWindowTitle();
  QTimer::singleShot(0, this, &MainWindow::frameGraphView);
}

MainWindow::~MainWindow() = default;

void MainWindow::createMenus()
{
  auto *fileMenu = menuBar()->addMenu(QStringLiteral("&Datei"));

  auto *newAction = fileMenu->addAction(QStringLiteral("Neu"));
  newAction->setShortcut(QKeySequence::New);
  connect(newAction, &QAction::triggered, this, &MainWindow::newDocument);

  auto *openAction = fileMenu->addAction(QStringLiteral("Öffnen…"));
  openAction->setShortcut(QKeySequence::Open);
  connect(openAction, &QAction::triggered, this, &MainWindow::openDocument);

  auto *saveAction = fileMenu->addAction(QStringLiteral("Speichern"));
  saveAction->setShortcut(QKeySequence::Save);
  connect(saveAction, &QAction::triggered, this, &MainWindow::saveDocument);

  auto *saveAsAction = fileMenu->addAction(QStringLiteral("Speichern unter…"));
  saveAsAction->setShortcut(QKeySequence::SaveAs);
  connect(saveAsAction, &QAction::triggered, this, &MainWindow::saveDocumentAs);
}

void MainWindow::registerNodeTypes()
{
  PlaybackState *playback = m_playback;
  m_registry->registerModel<InputNodeModel>(
    [playback]() { return std::make_unique<InputNodeModel>(playback); },
    QStringLiteral("IO"));
  m_registry->registerModel<OutputNodeModel>(QStringLiteral("IO"));
}

void MainWindow::bindNode(unsigned int nodeId)
{
  if (auto *input = m_graphModel->delegateModel<InputNodeModel>(nodeId)) {
    connect(input, &InputNodeModel::sequenceChanged, this, &MainWindow::updateFrameCount);
    connect(input, &QtNodes::NodeDelegateModel::dataUpdated, this, [this, nodeId](QtNodes::PortIndex) {
      if (inspects(nodeId)) {
        refreshInspectedView();
      }
    });
    updateFrameCount();
  }
  if (auto *output = m_graphModel->delegateModel<OutputNodeModel>(nodeId)) {
    connect(output, &OutputNodeModel::imageChanged, this, [this, nodeId](QImage const &) {
      if (inspects(nodeId)) {
        refreshInspectedView();
      }
    });
  }
}

void MainWindow::seedDefaultGraph()
{
  QtNodes::NodeId const inputId = m_graphModel->addNode(QStringLiteral("Input"));
  QtNodes::NodeId const outputId = m_graphModel->addNode(QStringLiteral("Output"));
  m_graphModel->setNodeData(inputId, QtNodes::NodeRole::Position, QPointF(80.0, 160.0));
  m_graphModel->setNodeData(outputId, QtNodes::NodeRole::Position, QPointF(420.0, 160.0));
  m_graphModel->addConnection(QtNodes::ConnectionId{inputId, 0, outputId, 0});
  inspectNode(outputId);
}

void MainWindow::frameGraphView()
{
  if (!m_graphView) {
    return;
  }
  m_graphView->zoomFitAll();
}

void MainWindow::updateFrameCount()
{
  int maxCount = 0;
  for (QtNodes::NodeId const id : m_graphModel->allNodeIds()) {
    if (auto const *input = m_graphModel->delegateModel<InputNodeModel>(id)) {
      maxCount = std::max(maxCount, input->frameCount());
    }
  }
  m_playback->setFrameCount(maxCount);
  m_viewer->setFrameRange(maxCount);
  m_viewer->setFrame(m_playback->frame());
}

void MainWindow::updateWindowTitle()
{
  QString const name = m_documentPath.isEmpty()
                         ? QStringLiteral("Unbenannt")
                         : QFileInfo(m_documentPath).fileName();
  setWindowTitle(QStringLiteral("%1 — NR OFX Host").arg(name));
}

bool MainWindow::saveTo(QString const &path)
{
  Pipeline const pipeline = PipelineCodec::capture(*m_graphModel, m_playback->frame());
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    QMessageBox::warning(this, QStringLiteral("Speichern"), file.errorString());
    return false;
  }
  file.write(QJsonDocument(pipeline.toJson()).toJson(QJsonDocument::Indented));
  m_documentPath = path;
  updateWindowTitle();
  statusBar()->showMessage(QStringLiteral("Gespeichert: %1").arg(path), 4000);
  return true;
}

void MainWindow::newDocument()
{
  auto ids = m_graphModel->allNodeIds();
  for (QtNodes::NodeId const id : ids) {
    m_graphModel->deleteNode(id);
  }
  m_documentPath.clear();
  m_playback->setFrameCount(0);
  m_playback->setFrame(0);
  m_inspectKind = InspectKind::None;
  m_inspectNodeId = QtNodes::InvalidNodeId;
  m_viewer->setImage({});
  m_viewer->setSource(QStringLiteral("Viewer"));
  seedDefaultGraph();
  updateWindowTitle();
}

void MainWindow::openDocument()
{
  QString const path = QFileDialog::getOpenFileName(this,
                                                    QStringLiteral("Pipeline öffnen"),
                                                    m_documentPath,
                                                    QStringLiteral("Pipeline (*.json)"));
  if (path.isEmpty()) {
    return;
  }

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, QStringLiteral("Öffnen"), file.errorString());
    return;
  }

  QJsonParseError parseError{};
  QJsonDocument const document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    QMessageBox::warning(this, QStringLiteral("Öffnen"), QStringLiteral("Ungültiges JSON."));
    return;
  }

  QString error;
  Pipeline const pipeline = Pipeline::fromJson(document.object(), &error);
  if (!error.isEmpty()) {
    QMessageBox::warning(this, QStringLiteral("Öffnen"), error);
    return;
  }

  try {
    PipelineCodec::restore(*m_graphModel, pipeline);
  } catch (std::exception const &ex) {
    QMessageBox::warning(this, QStringLiteral("Öffnen"), QString::fromLocal8Bit(ex.what()));
    return;
  }

  m_documentPath = path;
  updateFrameCount();
  m_playback->setFrame(pipeline.currentFrame);
  updateWindowTitle();
  frameGraphView();

  m_inspectKind = InspectKind::None;
  bool inspected = false;
  for (QtNodes::NodeId const id : m_graphModel->allNodeIds()) {
    if (m_graphModel->delegateModel<OutputNodeModel>(id)) {
      inspectNode(id);
      inspected = true;
      break;
    }
  }
  if (!inspected) {
    auto const ids = m_graphModel->allNodeIds();
    if (!ids.empty()) {
      inspectNode(*ids.begin());
    }
  }
}

bool MainWindow::saveDocument()
{
  if (m_documentPath.isEmpty()) {
    return saveDocumentAs();
  }
  return saveTo(m_documentPath);
}

bool MainWindow::saveDocumentAs()
{
  QString const path = QFileDialog::getSaveFileName(this,
                                                    QStringLiteral("Pipeline speichern"),
                                                    m_documentPath,
                                                    QStringLiteral("Pipeline (*.json)"));
  if (path.isEmpty()) {
    return false;
  }
  return saveTo(path);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
  if (watched == m_scene && event->type() == QEvent::GraphicsSceneMousePress) {
    auto const *mouse = static_cast<QGraphicsSceneMouseEvent *>(event);
    if (mouse->button() == Qt::LeftButton) {
      for (QGraphicsItem *item : m_scene->items(mouse->scenePos())) {
        if (auto *nodeItem = qgraphicsitem_cast<QtNodes::NodeGraphicsObject *>(item)) {
          QPointF const nodeCoord = nodeItem->sceneTransform().inverted().map(mouse->scenePos());
          QtNodes::AbstractNodeGeometry &geometry = m_scene->nodeGeometry();
          for (QtNodes::PortType const portType : {QtNodes::PortType::In, QtNodes::PortType::Out}) {
            QtNodes::PortIndex const portIndex =
              geometry.checkPortHit(nodeItem->nodeId(), portType, nodeCoord);
            if (portIndex != QtNodes::InvalidPortIndex) {
              m_skipNodeSelect = true;
              inspectPort(nodeItem->nodeId(), portType, portIndex);
              break;
            }
          }
          break;
        }
        if (auto *connectionItem = qgraphicsitem_cast<QtNodes::ConnectionGraphicsObject *>(item)) {
          inspectConnection(connectionItem->connectionId());
          break;
        }
      }
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::inspectNode(QtNodes::NodeId nodeId)
{
  m_inspectKind = InspectKind::Node;
  m_inspectNodeId = nodeId;
  refreshInspectedView();
}

void MainWindow::inspectPort(QtNodes::NodeId nodeId,
                             QtNodes::PortType portType,
                             QtNodes::PortIndex portIndex)
{
  m_inspectKind = InspectKind::Port;
  m_inspectNodeId = nodeId;
  m_inspectPortType = portType;
  m_inspectPortIndex = portIndex;
  refreshInspectedView();
}

void MainWindow::inspectConnection(QtNodes::ConnectionId const &connectionId)
{
  m_inspectKind = InspectKind::Connection;
  m_inspectConnection = connectionId;
  m_inspectNodeId = connectionId.outNodeId;
  refreshInspectedView();
}

void MainWindow::onGraphSelectionChanged()
{
  if (!m_scene) {
    return;
  }
  for (QGraphicsItem *item : m_scene->selectedItems()) {
    if (auto *connectionItem = qgraphicsitem_cast<QtNodes::ConnectionGraphicsObject *>(item)) {
      inspectConnection(connectionItem->connectionId());
      return;
    }
  }
}

void MainWindow::refreshInspectedView()
{
  QImage image;
  QString source = QStringLiteral("Viewer");

  auto nodeName = [this](QtNodes::NodeId const id) {
    return m_graphModel->nodeData(id, QtNodes::NodeRole::Type).toString();
  };

  switch (m_inspectKind) {
  case InspectKind::None:
    break;
  case InspectKind::Node: {
    if (m_graphModel->delegateModel<InputNodeModel>(m_inspectNodeId)) {
      image = imageAtPort(m_inspectNodeId, QtNodes::PortType::Out, 0);
      source = QStringLiteral("Viewer · %1").arg(nodeName(m_inspectNodeId));
    } else if (m_graphModel->delegateModel<OutputNodeModel>(m_inspectNodeId)) {
      image = imageAtPort(m_inspectNodeId, QtNodes::PortType::In, 0);
      source = QStringLiteral("Viewer · %1").arg(nodeName(m_inspectNodeId));
    } else {
      image = imageAtPort(m_inspectNodeId, QtNodes::PortType::Out, 0);
      if (image.isNull()) {
        image = imageAtPort(m_inspectNodeId, QtNodes::PortType::In, 0);
      }
      source = QStringLiteral("Viewer · %1").arg(nodeName(m_inspectNodeId));
    }
    break;
  }
  case InspectKind::Port: {
    image = imageAtPort(m_inspectNodeId, m_inspectPortType, m_inspectPortIndex);
    QString const side = m_inspectPortType == QtNodes::PortType::In ? QStringLiteral("In")
                                                                    : QStringLiteral("Out");
    source = QStringLiteral("Viewer · %1 · %2 %3")
               .arg(nodeName(m_inspectNodeId), side)
               .arg(m_inspectPortIndex);
    break;
  }
  case InspectKind::Connection: {
    image = imageAtPort(m_inspectConnection.outNodeId,
                        QtNodes::PortType::Out,
                        m_inspectConnection.outPortIndex);
    source = QStringLiteral("Viewer · Verbindung %1 → %2")
               .arg(nodeName(m_inspectConnection.outNodeId),
                    nodeName(m_inspectConnection.inNodeId));
    break;
  }
  }

  m_viewer->setSource(source);
  m_viewer->setImage(image);
}

QImage MainWindow::imageAtPort(QtNodes::NodeId nodeId,
                               QtNodes::PortType portType,
                               QtNodes::PortIndex portIndex)
{
  if (portType == QtNodes::PortType::Out) {
    QVariant const variant =
      m_graphModel->portData(nodeId, portType, portIndex, QtNodes::PortRole::Data);
    auto const data = variant.value<std::shared_ptr<QtNodes::NodeData>>();
    if (auto const imageData = std::dynamic_pointer_cast<ImageData>(data)) {
      return imageData->image();
    }
    return {};
  }

  if (auto const *output = m_graphModel->delegateModel<OutputNodeModel>(nodeId)) {
    return output->currentImage();
  }

  auto const connections = m_graphModel->connections(nodeId, portType, portIndex);
  if (!connections.empty()) {
    QtNodes::ConnectionId const connection = *connections.begin();
    return imageAtPort(connection.outNodeId, QtNodes::PortType::Out, connection.outPortIndex);
  }
  return {};
}

bool MainWindow::inspects(QtNodes::NodeId nodeId) const
{
  if (m_inspectKind == InspectKind::Node || m_inspectKind == InspectKind::Port) {
    return m_inspectNodeId == nodeId;
  }
  if (m_inspectKind == InspectKind::Connection) {
    return m_inspectConnection.outNodeId == nodeId || m_inspectConnection.inNodeId == nodeId;
  }
  return false;
}
