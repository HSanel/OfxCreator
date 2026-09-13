#include "HostGraphicsView.h"

#include <QtNodes/BasicGraphicsScene>

#include <QMouseEvent>
#include <QScrollBar>
#include <QShowEvent>

HostGraphicsView::HostGraphicsView(QtNodes::BasicGraphicsScene *scene, QWidget *parent)
  : QtNodes::GraphicsView(scene, parent)
{
  setDragMode(QGraphicsView::NoDrag);
  // QtNodes setzt ±32767 als View-sceneRect. Das zerlegt Zoom/Pan und den Hintergrund.
  QGraphicsView::setSceneRect(QRectF());
}

void HostGraphicsView::showEvent(QShowEvent *event)
{
  QGraphicsView::showEvent(event);
}

void HostGraphicsView::mousePressEvent(QMouseEvent *event)
{
  bool const panButton = event->button() == Qt::MiddleButton
                         || (event->button() == Qt::LeftButton && itemAt(event->pos()) == nullptr);
  if (panButton) {
    m_panning = true;
    m_lastPanPos = event->pos();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
    return;
  }
  QtNodes::GraphicsView::mousePressEvent(event);
}

void HostGraphicsView::mouseMoveEvent(QMouseEvent *event)
{
  if (m_panning) {
    QPoint const delta = event->pos() - m_lastPanPos;
    m_lastPanPos = event->pos();
    if (horizontalScrollBar()) {
      horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
    }
    if (verticalScrollBar()) {
      verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
    }
    event->accept();
    return;
  }
  QGraphicsView::mouseMoveEvent(event);
}

void HostGraphicsView::mouseReleaseEvent(QMouseEvent *event)
{
  if (m_panning
      && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
    m_panning = false;
    unsetCursor();
    event->accept();
    return;
  }
  QGraphicsView::mouseReleaseEvent(event);
}
