#include "HostGraphicsView.h"

#include <QtNodes/BasicGraphicsScene>
#include <QtNodes/Definitions>
#include <QtNodes/internal/AbstractNodeGeometry.hpp>

#include <QApplication>
#include <QGraphicsProxyWidget>
#include <QKeyEvent>
#include <QLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QShowEvent>
#include <QWheelEvent>

#include <algorithm>

namespace {
constexpr qreal kResizeMarginView = 14.0;
QSize const kMinKernelWidget(220, 140);

void applyResizeCursor(QWidget *widget, int edges)
{
  using HV = HostGraphicsView;
  bool const horizontal = (edges & HV::ResizeLeft) || (edges & HV::ResizeRight);
  bool const vertical = (edges & HV::ResizeTop) || (edges & HV::ResizeBottom);
  if (horizontal && vertical) {
    if (((edges & HV::ResizeLeft) && (edges & HV::ResizeTop))
        || ((edges & HV::ResizeRight) && (edges & HV::ResizeBottom))) {
      widget->setCursor(Qt::SizeFDiagCursor);
    } else {
      widget->setCursor(Qt::SizeBDiagCursor);
    }
  } else if (horizontal) {
    widget->setCursor(Qt::SizeHorCursor);
  } else if (vertical) {
    widget->setCursor(Qt::SizeVerCursor);
  } else {
    widget->unsetCursor();
  }
}
} // namespace

HostGraphicsView::HostGraphicsView(QtNodes::BasicGraphicsScene *scene, QWidget *parent)
  : QtNodes::GraphicsView(scene, parent)
{
  setDragMode(QGraphicsView::NoDrag);
  setMouseTracking(true);
  viewport()->setMouseTracking(true);
  // QtNodes setzt ±32767 als View-sceneRect. Das zerlegt Zoom/Pan und den Hintergrund.
  QGraphicsView::setSceneRect(QRectF());
}

void HostGraphicsView::showEvent(QShowEvent *event)
{
  QGraphicsView::showEvent(event);
}

QWidget *HostGraphicsView::embeddedWidgetAt(QPoint const &viewPos) const
{
  QGraphicsItem *item = itemAt(viewPos);
  QGraphicsProxyWidget *proxy = nullptr;
  for (QGraphicsItem *it = item; it; it = it->parentItem()) {
    proxy = qgraphicsitem_cast<QGraphicsProxyWidget *>(it);
    if (proxy) {
      break;
    }
  }
  if (!proxy || !proxy->widget()) {
    return nullptr;
  }
  QPoint const local = proxy->mapFromScene(mapToScene(viewPos)).toPoint();
  QWidget *child = proxy->widget()->childAt(local);
  return child ? child : proxy->widget();
}

QPlainTextEdit *HostGraphicsView::plainTextEditAt(QPoint const &viewPos) const
{
  QWidget *hit = embeddedWidgetAt(viewPos);
  while (hit) {
    if (auto *edit = qobject_cast<QPlainTextEdit *>(hit)) {
      return edit;
    }
    hit = hit->parentWidget();
  }
  return nullptr;
}

bool HostGraphicsView::embeddedEditorHasFocus() const
{
  if (m_activeTextEdit) {
    return true;
  }
  QWidget *fw = QApplication::focusWidget();
  return qobject_cast<QPlainTextEdit *>(fw) != nullptr || qobject_cast<QLineEdit *>(fw) != nullptr;
}

void HostGraphicsView::clearEmbeddedFocus()
{
  m_activeTextEdit.clear();
  if (scene()) {
    for (QGraphicsItem *item : scene()->items()) {
      auto *proxy = qgraphicsitem_cast<QGraphicsProxyWidget *>(item);
      if (!proxy || !proxy->widget()) {
        continue;
      }
      for (QPlainTextEdit *edit : proxy->widget()->findChildren<QPlainTextEdit *>()) {
        edit->clearFocus();
      }
      for (QLineEdit *line : proxy->widget()->findChildren<QLineEdit *>()) {
        line->clearFocus();
      }
    }
    scene()->clearFocus();
  }
  if (QWidget *fw = QApplication::focusWidget()) {
    if (qobject_cast<QPlainTextEdit *>(fw) || qobject_cast<QLineEdit *>(fw)) {
      fw->clearFocus();
    }
  }
  viewport()->setFocus(Qt::MouseFocusReason);
}

QtNodes::NodeGraphicsObject *HostGraphicsView::nodeItemAt(QPoint const &viewPos) const
{
  int const pad = qRound(kResizeMarginView);
  QRect const probe(viewPos.x() - pad, viewPos.y() - pad, pad * 2 + 1, pad * 2 + 1);
  for (QGraphicsItem *item : items(probe)) {
    for (QGraphicsItem *it = item; it; it = it->parentItem()) {
      if (auto *ngo = qgraphicsitem_cast<QtNodes::NodeGraphicsObject *>(it)) {
        return ngo;
      }
    }
  }
  return nullptr;
}

int HostGraphicsView::resizeEdgesAt(QPoint const &viewPos) const
{
  QtNodes::NodeGraphicsObject *ngo = nodeItemAt(viewPos);
  if (!ngo) {
    return ResizeNone;
  }
  if (!(ngo->graphModel().nodeFlags(ngo->nodeId()) & QtNodes::NodeFlag::Resizable)) {
    return ResizeNone;
  }

  QPointF const itemPos = ngo->mapFromScene(mapToScene(viewPos));
  QtNodes::AbstractNodeGeometry &geometry = ngo->nodeScene()->nodeGeometry();
  for (QtNodes::PortType const portType : {QtNodes::PortType::In, QtNodes::PortType::Out}) {
    if (geometry.checkPortHit(ngo->nodeId(), portType, itemPos) != QtNodes::InvalidPortIndex) {
      return ResizeNone;
    }
  }

  QRectF const bounds = ngo->boundingRect();
  qreal const scale = std::max(0.2, transform().m11());
  qreal const margin = kResizeMarginView / scale;
  if (!bounds.adjusted(-margin, -margin, margin, margin).contains(itemPos)) {
    return ResizeNone;
  }

  int edges = ResizeNone;
  if (itemPos.x() <= bounds.left() + margin) {
    edges |= ResizeLeft;
  }
  if (itemPos.x() >= bounds.right() - margin) {
    edges |= ResizeRight;
  }
  if (itemPos.y() <= bounds.top() + margin) {
    edges |= ResizeTop;
  }
  if (itemPos.y() >= bounds.bottom() - margin) {
    edges |= ResizeBottom;
  }
  return edges;
}

void HostGraphicsView::updateHoverCursor(QPoint const &viewPos)
{
  int const edges = resizeEdgesAt(viewPos);
  applyResizeCursor(this, edges);
  applyResizeCursor(viewport(), edges);
  if (QtNodes::NodeGraphicsObject *ngo = nodeItemAt(viewPos)) {
    if (edges == ResizeNone) {
      ngo->unsetCursor();
    } else {
      applyResizeCursor(viewport(), edges);
      ngo->setCursor(viewport()->cursor());
    }
  }
}

void HostGraphicsView::applyNodeResize(QPoint const &viewPos)
{
  if (!m_resizeNode) {
    return;
  }

  QPointF const itemPos = m_resizeNode->mapFromScene(mapToScene(viewPos));
  QPointF const delta = itemPos - m_lastResizeItemPos;
  m_lastResizeItemPos = itemPos;

  QtNodes::AbstractGraphModel &model = m_resizeNode->graphModel();
  QtNodes::NodeId const nodeId = m_resizeNode->nodeId();
  QWidget *widget = model.nodeData(nodeId, QtNodes::NodeRole::Widget).value<QWidget *>();
  if (!widget) {
    return;
  }

  QSize size = widget->size();
  QPointF pos = m_resizeNode->pos();
  if (m_resizeEdges & ResizeRight) {
    size.rwidth() += qRound(delta.x());
  }
  if (m_resizeEdges & ResizeBottom) {
    size.rheight() += qRound(delta.y());
  }
  if (m_resizeEdges & ResizeLeft) {
    size.rwidth() -= qRound(delta.x());
    pos.rx() += delta.x();
  }
  if (m_resizeEdges & ResizeTop) {
    size.rheight() -= qRound(delta.y());
    pos.ry() += delta.y();
  }
  size.setWidth(std::max(kMinKernelWidget.width(), size.width()));
  size.setHeight(std::max(kMinKernelWidget.height(), size.height()));

  m_resizeNode->setGeometryChanged();
  widget->setFixedSize(size);
  if (widget->layout()) {
    widget->layout()->activate();
  }
  widget->updateGeometry();
  for (QGraphicsItem *child : m_resizeNode->childItems()) {
    if (auto *proxy = qgraphicsitem_cast<QGraphicsProxyWidget *>(child)) {
      proxy->setMinimumSize(0, 0);
      proxy->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
      proxy->resize(size);
    }
  }
  if (m_resizeNode->nodeScene()) {
    m_resizeNode->nodeScene()->nodeGeometry().recomputeSize(nodeId);
    m_resizeNode->updateQWidgetEmbedPos();
  }
  if ((m_resizeEdges & ResizeLeft) || (m_resizeEdges & ResizeTop)) {
    m_resizeNode->setPos(pos);
  }
  m_resizeNode->update();
  m_resizeNode->moveConnections();
}

void HostGraphicsView::mousePressEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton) {
    int const edges = resizeEdgesAt(event->pos());
    if (edges != ResizeNone) {
      m_resizing = true;
      m_resizeEdges = edges;
      m_resizeNode = nodeItemAt(event->pos());
      m_lastResizeItemPos = m_resizeNode->mapFromScene(mapToScene(event->pos()));
      event->accept();
      return;
    }
  }

  bool const emptyCanvas = itemAt(event->pos()) == nullptr;
  bool const panButton = event->button() == Qt::MiddleButton
                         || (event->button() == Qt::LeftButton && emptyCanvas);
  if (emptyCanvas) {
    clearEmbeddedFocus();
  } else if (event->button() == Qt::LeftButton) {
    if (QPlainTextEdit *edit = plainTextEditAt(event->pos())) {
      m_activeTextEdit = edit;
    } else {
      m_activeTextEdit.clear();
      if (auto *edit = qobject_cast<QPlainTextEdit *>(QApplication::focusWidget())) {
        edit->clearFocus();
      }
    }
  }
  if (panButton) {
    m_panning = true;
    m_lastPanPos = event->pos();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
    return;
  }
  QtNodes::GraphicsView::mousePressEvent(event);
  if (m_activeTextEdit) {
    m_activeTextEdit->setFocus(Qt::MouseFocusReason);
  }
}

void HostGraphicsView::mouseMoveEvent(QMouseEvent *event)
{
  if (m_resizing) {
    applyNodeResize(event->pos());
    event->accept();
    return;
  }
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
  if (m_resizing) {
    applyNodeResize(event->pos());
    event->accept();
    return;
  }
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
  if (resizeEdgesAt(event->pos()) != ResizeNone) {
    updateHoverCursor(event->pos());
    event->accept();
    return;
  }
  unsetCursor();
  viewport()->unsetCursor();
  QGraphicsView::mouseMoveEvent(event);
}

void HostGraphicsView::mouseReleaseEvent(QMouseEvent *event)
{
  if (m_resizing && event->button() == Qt::LeftButton) {
    m_resizing = false;
    m_resizeNode = nullptr;
    m_resizeEdges = ResizeNone;
    updateHoverCursor(event->pos());
    event->accept();
    return;
  }
  if (m_panning
      && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
    m_panning = false;
    unsetCursor();
    event->accept();
    return;
  }
  QGraphicsView::mouseReleaseEvent(event);
}

void HostGraphicsView::wheelEvent(QWheelEvent *event)
{
  QPoint const viewPos = event->position().toPoint();
  QPlainTextEdit *edit = plainTextEditAt(viewPos);
  if (edit && m_activeTextEdit == edit) {
    int dy = event->pixelDelta().y();
    int dx = event->pixelDelta().x();
    if (dy == 0 && dx == 0) {
      int const line = std::max(1, edit->fontMetrics().lineSpacing());
      auto notches = [](int angle) {
        if (angle == 0) {
          return 0;
        }
        int const n = angle / 120;
        return n == 0 ? (angle > 0 ? 1 : -1) : n;
      };
      dy = notches(event->angleDelta().y()) * line * 3;
      dx = notches(event->angleDelta().x()) * line * 3;
    }
    if (event->modifiers() & Qt::ShiftModifier) {
      std::swap(dx, dy);
    }
    if (QScrollBar *vbar = edit->verticalScrollBar()) {
      vbar->setValue(vbar->value() - dy);
    }
    if (dx != 0) {
      if (QScrollBar *hbar = edit->horizontalScrollBar()) {
        hbar->setValue(hbar->value() - dx);
      }
    }
    event->accept();
    return;
  }
  QtNodes::GraphicsView::wheelEvent(event);
}

void HostGraphicsView::keyPressEvent(QKeyEvent *event)
{
  if (embeddedEditorHasFocus()) {
    QGraphicsView::keyPressEvent(event);
    return;
  }
  QtNodes::GraphicsView::keyPressEvent(event);
}
