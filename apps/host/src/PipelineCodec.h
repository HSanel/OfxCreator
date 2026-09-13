#pragma once

#include "ir/Pipeline.h"

namespace QtNodes {
class DataFlowGraphModel;
}

class PipelineCodec
{
public:
  static Pipeline capture(QtNodes::DataFlowGraphModel &graph, int currentFrame);
  static bool restore(QtNodes::DataFlowGraphModel &graph, Pipeline const &pipeline);
};
