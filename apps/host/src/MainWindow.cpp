#include "MainWindow.h"

#include "OfxSdkProbe.h"

#include <QtNodes/DataFlowGraphicsScene>
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/GraphicsView>
#include <QtNodes/NodeDelegateModelRegistry>

#include <QFrame>
#include <QLabel>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>

#include <QImage>
#include <QPixmap>

MainWindow::MainWindow(QWidget *parent)
  : QMainWindow(parent)
  , m_registry(std::make_shared<QtNodes::NodeDelegateModelRegistry>())
  , m_graphModel(std::make_unique<QtNodes::DataFlowGraphModel>(m_registry))
{
  setWindowTitle(QStringLiteral("NR OFX Host"));
  resize(1280, 720);

  auto *splitter = new QSplitter(Qt::Horizontal, this);

  auto *scene = new QtNodes::DataFlowGraphicsScene(*m_graphModel, splitter);
  auto *graphView = new QtNodes::GraphicsView(scene);
  graphView->setScene(scene);

  auto *viewerFrame = new QFrame;
  viewerFrame->setMinimumWidth(360);
  auto *viewerLayout = new QVBoxLayout(viewerFrame);

  auto *caption = new QLabel(QStringLiteral("Viewer"));
  caption->setAlignment(Qt::AlignCenter);

  QImage gray(320, 180, QImage::Format_RGB32);
  gray.fill(QColor(48, 48, 48));
  auto *preview = new QLabel;
  preview->setAlignment(Qt::AlignCenter);
  preview->setPixmap(QPixmap::fromImage(gray));
  preview->setFrameShape(QFrame::StyledPanel);

  auto *hint = new QLabel(QStringLiteral("Stufe 0 — leere Pipeline, grauer Testframe"));
  hint->setAlignment(Qt::AlignCenter);
  hint->setWordWrap(true);

  viewerLayout->addWidget(caption);
  viewerLayout->addWidget(preview, 1);
  viewerLayout->addWidget(hint);

  splitter->addWidget(graphView);
  splitter->addWidget(viewerFrame);
  splitter->setStretchFactor(0, 3);
  splitter->setStretchFactor(1, 1);

  setCentralWidget(splitter);
  statusBar()->showMessage(ofxSdkVersionLabel());
}

MainWindow::~MainWindow() = default;
