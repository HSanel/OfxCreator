#pragma once

#include <QtNodes/GraphicsView>

#include <QPoint>

class HostGraphicsView : public QtNodes::GraphicsView
{
public:
  explicit HostGraphicsView(QtNodes::BasicGraphicsScene *scene, QWidget *parent = nullptr);

protected:
  void showEvent(QShowEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private:
  bool m_panning = false;
  QPoint m_lastPanPos;
};
