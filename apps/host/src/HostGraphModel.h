#pragma once

#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/Definitions>

class HostGraphModel : public QtNodes::DataFlowGraphModel
{
public:
  using DataFlowGraphModel::DataFlowGraphModel;

  bool connectionPossible(QtNodes::ConnectionId const connectionId) const override;
  void addConnection(QtNodes::ConnectionId const connectionId) override;
};
