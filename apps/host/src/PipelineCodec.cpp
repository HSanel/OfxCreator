#include "PipelineCodec.h"

#include "nodes/InputNodeModel.h"

#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/Definitions>
#include <QtNodes/NodeDelegateModel>

#include <QJsonArray>
#include <QJsonObject>

namespace {

void clearGraph(QtNodes::DataFlowGraphModel &graph)
{
  auto ids = graph.allNodeIds();
  for (QtNodes::NodeId const id : ids) {
    graph.deleteNode(id);
  }
}

} // namespace

Pipeline PipelineCodec::capture(QtNodes::DataFlowGraphModel &graph, int currentFrame)
{
  Pipeline pipeline;
  pipeline.currentFrame = currentFrame;

  for (QtNodes::NodeId const id : graph.allNodeIds()) {
    PipelineNode node;
    node.id = id;
    QString const type = graph.nodeData(id, QtNodes::NodeRole::Type).toString();
    node.kind = nodeKindFromId(type);
    QPointF const pos = graph.nodeData(id, QtNodes::NodeRole::Position).value<QPointF>();
    node.x = pos.x();
    node.y = pos.y();

    if (auto const *input = graph.delegateModel<InputNodeModel>(id)) {
      node.params.insert(QStringLiteral("path"), input->sequencePath());
    }
    pipeline.nodes.push_back(node);

    for (auto const &connection : graph.allConnectionIds(id)) {
      if (connection.outNodeId != id) {
        continue;
      }
      PipelineConnection edge;
      edge.fromId = connection.outNodeId;
      edge.fromPort = connection.outPortIndex;
      edge.toId = connection.inNodeId;
      edge.toPort = connection.inPortIndex;
      pipeline.connections.push_back(edge);
    }
  }

  return pipeline;
}

bool PipelineCodec::restore(QtNodes::DataFlowGraphModel &graph, Pipeline const &pipeline)
{
  clearGraph(graph);

  QJsonObject qtNodesJson;
  QJsonArray nodes;
  for (PipelineNode const &node : pipeline.nodes) {
    QJsonObject internal;
    internal.insert(QStringLiteral("model-name"), nodeKindId(node.kind));
    if (node.kind == NodeKind::Input) {
      internal.insert(QStringLiteral("path"), node.params.value(QStringLiteral("path")).toString());
    }

    QJsonObject nodeJson;
    nodeJson.insert(QStringLiteral("id"), static_cast<qint64>(node.id));
    nodeJson.insert(QStringLiteral("internal-data"), internal);
    QJsonObject position;
    position.insert(QStringLiteral("x"), node.x);
    position.insert(QStringLiteral("y"), node.y);
    nodeJson.insert(QStringLiteral("position"), position);
    nodes.append(nodeJson);
  }

  QJsonArray connections;
  for (PipelineConnection const &connection : pipeline.connections) {
    QJsonObject json;
    json.insert(QStringLiteral("outNodeId"), static_cast<qint64>(connection.fromId));
    json.insert(QStringLiteral("outPortIndex"), connection.fromPort);
    json.insert(QStringLiteral("inNodeId"), static_cast<qint64>(connection.toId));
    json.insert(QStringLiteral("inPortIndex"), connection.toPort);
    connections.append(json);
  }

  qtNodesJson.insert(QStringLiteral("nodes"), nodes);
  qtNodesJson.insert(QStringLiteral("connections"), connections);
  graph.load(qtNodesJson);
  return true;
}
