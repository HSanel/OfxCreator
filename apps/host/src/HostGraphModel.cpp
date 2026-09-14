#include "HostGraphModel.h"

#include <QtNodes/ConnectionIdUtils>
#include <QtNodes/NodeData>

#include <stack>
#include <vector>

using QtNodes::ConnectionId;
using QtNodes::ConnectionPolicy;
using QtNodes::NodeDataType;
using QtNodes::NodeId;
using QtNodes::NodeRole;
using QtNodes::PortIndex;
using QtNodes::PortRole;
using QtNodes::PortType;

bool HostGraphModel::connectionPossible(ConnectionId const connectionId) const
{
  if (!nodeExists(connectionId.outNodeId) || !nodeExists(connectionId.inNodeId)) {
    return false;
  }

  auto checkPortBounds = [&](PortType const portType) {
    NodeId const nodeId = QtNodes::getNodeId(portType, connectionId);
    auto const portCountRole = (portType == PortType::Out) ? NodeRole::OutPortCount
                                                           : NodeRole::InPortCount;
    std::size_t const portCount = nodeData(nodeId, portCountRole).toUInt();
    return QtNodes::getPortIndex(portType, connectionId) < portCount;
  };

  auto getDataType = [&](PortType const portType) {
    return portData(QtNodes::getNodeId(portType, connectionId),
                    portType,
                    QtNodes::getPortIndex(portType, connectionId),
                    PortRole::DataType)
        .value<NodeDataType>();
  };

  auto outPortVacant = [&]() {
    NodeId const nodeId = connectionId.outNodeId;
    PortIndex const portIndex = connectionId.outPortIndex;
    auto const connected = connections(nodeId, PortType::Out, portIndex);
    auto const policy = portData(nodeId, PortType::Out, portIndex, PortRole::ConnectionPolicyRole)
                            .value<ConnectionPolicy>();
    return connected.empty() || policy == ConnectionPolicy::Many;
  };

  if (getDataType(PortType::Out).id != getDataType(PortType::In).id
      || !outPortVacant() || !checkPortBounds(PortType::Out) || !checkPortBounds(PortType::In)) {
    return false;
  }

  if (loopsEnabled()) {
    return true;
  }

  std::stack<NodeId> filo;
  filo.push(connectionId.inNodeId);
  while (!filo.empty()) {
    NodeId const id = filo.top();
    filo.pop();
    if (id == connectionId.outNodeId) {
      return false;
    }
    std::size_t const nOutPorts = nodeData(id, NodeRole::OutPortCount).toUInt();
    for (PortIndex index = 0; index < nOutPorts; ++index) {
      for (ConnectionId const &cid : connections(id, PortType::Out, index)) {
        filo.push(cid.inNodeId);
      }
    }
  }
  return true;
}

void HostGraphModel::addConnection(ConnectionId const connectionId)
{
  auto const existing = connections(connectionId.inNodeId, PortType::In, connectionId.inPortIndex);
  std::vector<ConnectionId> toRemove;
  toRemove.reserve(existing.size());
  for (ConnectionId const &cid : existing) {
    if (cid.outNodeId != connectionId.outNodeId || cid.outPortIndex != connectionId.outPortIndex) {
      toRemove.push_back(cid);
    }
  }
  for (ConnectionId const &cid : toRemove) {
    deleteConnection(cid);
  }
  DataFlowGraphModel::addConnection(connectionId);
}
