#pragma once

#include <QtNodes/GraphicsView>
#include <QtNodes/internal/NodeGraphicsObject.hpp>

#include <QPoint>
#include <QPointF>
#include <QPointer>

class QWidget;
class QWheelEvent;
class QKeyEvent;
class QPlainTextEdit;

class HostGraphicsView : public QtNodes::GraphicsView
{
public:
  explicit HostGraphicsView(QtNodes::BasicGraphicsScene *scene, QWidget *parent = nullptr);

  enum ResizeEdge {
    ResizeNone = 0,
    ResizeLeft = 1,
    ResizeRight = 2,
    ResizeTop = 4,
    ResizeBottom = 8
  };

protected:
  void showEvent(QShowEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  QWidget *embeddedWidgetAt(QPoint const &viewPos) const;
  QPlainTextEdit *plainTextEditAt(QPoint const &viewPos) const;
  bool embeddedEditorHasFocus() const;
  void clearEmbeddedFocus();
  QtNodes::NodeGraphicsObject *nodeItemAt(QPoint const &viewPos) const;
  int resizeEdgesAt(QPoint const &viewPos) const;
  void updateHoverCursor(QPoint const &viewPos);
  void applyNodeResize(QPoint const &viewPos);

  bool m_panning = false;
  QPoint m_lastPanPos;
  bool m_resizing = false;
  int m_resizeEdges = ResizeNone;
  QtNodes::NodeGraphicsObject *m_resizeNode = nullptr;
  QPointF m_lastResizeItemPos;
  QPointer<QPlainTextEdit> m_activeTextEdit;
};
