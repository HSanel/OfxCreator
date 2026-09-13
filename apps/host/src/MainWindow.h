#pragma once

#include <QMainWindow>

#include <memory>

namespace QtNodes {
class DataFlowGraphModel;
class DataFlowGraphicsScene;
class NodeDelegateModelRegistry;
}

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

private:
  std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_registry;
  std::unique_ptr<QtNodes::DataFlowGraphModel> m_graphModel;
};
