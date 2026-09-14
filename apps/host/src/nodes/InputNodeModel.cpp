#include "InputNodeModel.h"

#include "ImageData.h"
#include "PlaybackState.h"

#include "io/VideoIngest.h"

#include <QApplication>
#include <QFileDialog>
#include <QMainWindow>
#include <QMessageBox>
#include <QProgressDialog>
#include <QTimer>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

InputNodeModel::InputNodeModel(PlaybackState *playback)
  : m_playback(playback)
{
  if (m_playback) {
    connect(m_playback, &PlaybackState::frameChanged, this, [this](int) { emitCurrentFrame(); });
  }
}

unsigned int InputNodeModel::nPorts(QtNodes::PortType portType) const
{
  return portType == QtNodes::PortType::Out ? 1 : 0;
}

QtNodes::NodeDataType InputNodeModel::dataType(QtNodes::PortType, QtNodes::PortIndex) const
{
  return ImageData().type();
}

std::shared_ptr<QtNodes::NodeData> InputNodeModel::outData(QtNodes::PortIndex)
{
  int const frame = m_playback ? m_playback->frame() : 0;
  QImage image = m_sequence.loadFrame(frame);
  if (image.isNull()) {
    return {};
  }
  return std::make_shared<ImageData>(std::move(image), frame);
}

QImage InputNodeModel::currentImage() const
{
  int const frame = m_playback ? m_playback->frame() : 0;
  return m_sequence.loadFrame(frame);
}

QWidget *InputNodeModel::embeddedWidget()
{
  if (m_widget) {
    return m_widget;
  }

  m_widget = new QWidget;
  auto *layout = new QVBoxLayout(m_widget);
  layout->setContentsMargins(4, 4, 4, 4);

  m_pathEdit = new QLineEdit;
  m_pathEdit->setPlaceholderText(QStringLiteral("Bild, Video oder Sequenzordner"));
  m_pathEdit->setReadOnly(true);
  refreshPathEdit();

  auto *buttons = new QWidget;
  auto *buttonLayout = new QHBoxLayout(buttons);
  buttonLayout->setContentsMargins(0, 0, 0, 0);
  auto *fileButton = new QPushButton(QStringLiteral("Datei…"));
  auto *folderButton = new QPushButton(QStringLiteral("Ordner…"));
  buttonLayout->addWidget(fileButton);
  buttonLayout->addWidget(folderButton);

  layout->addWidget(m_pathEdit);
  layout->addWidget(buttons);

  connect(fileButton, &QPushButton::clicked, this, &InputNodeModel::browseFile);
  connect(folderButton, &QPushButton::clicked, this, &InputNodeModel::browseFolder);

  m_widget->setMinimumWidth(220);
  return m_widget;
}

QJsonObject InputNodeModel::save() const
{
  QJsonObject json = NodeDelegateModel::save();
  json.insert(QStringLiteral("path"), m_sourcePath);
  return json;
}

void InputNodeModel::load(QJsonObject const &json)
{
  setSequencePath(json.value(QStringLiteral("path")).toString());
}

void InputNodeModel::setSequencePath(QString const &path)
{
  m_sourcePath = path;
  if (path.isEmpty()) {
    m_sequence.clear();
    refreshPathEdit();
    Q_EMIT sequenceChanged();
    emitCurrentFrame();
    return;
  }

  QString resolved = path;
  if (VideoIngest::isVideoFile(path)) {
    QProgressDialog progress(QStringLiteral("Video wird mit ffmpeg in eine Sequenz zerlegt…"),
                             QString(),
                             0,
                             0,
                             dialogParent());
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.show();
    QApplication::processEvents();

    QString error;
    resolved = VideoIngest::ensureSequence(path, &error);
    progress.close();
    if (resolved.isEmpty()) {
      QMessageBox::warning(dialogParent(), QStringLiteral("FFmpeg"), error);
      m_sequence.clear();
      refreshPathEdit();
      Q_EMIT sequenceChanged();
      emitCurrentFrame();
      return;
    }
  }

  m_sequence.setPath(resolved);
  refreshPathEdit();
  Q_EMIT sequenceChanged();
  emitCurrentFrame();
}

void InputNodeModel::browseFile()
{
  QTimer::singleShot(0, this, [this]() {
    QString const file = QFileDialog::getOpenFileName(
      dialogParent(),
      QStringLiteral("Bild oder Video wählen"),
      m_sourcePath.isEmpty() ? m_sequence.path() : m_sourcePath,
      QStringLiteral(
        "Medien (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp *.mp4 *.mov *.mkv *.avi *.webm *.m4v);;"
        "Bilder (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp);;"
        "Video (*.mp4 *.mov *.mkv *.avi *.webm *.m4v);;"
        "Alle Dateien (*.*)"));
    if (!file.isEmpty()) {
      setSequencePath(file);
    }
  });
}

void InputNodeModel::browseFolder()
{
  QTimer::singleShot(0, this, [this]() {
    QString const folder = QFileDialog::getExistingDirectory(dialogParent(),
                                                             QStringLiteral("Sequenzordner wählen"),
                                                             m_sourcePath.isEmpty() ? m_sequence.path()
                                                                                    : m_sourcePath);
    if (!folder.isEmpty()) {
      setSequencePath(folder);
    }
  });
}

QWidget *InputNodeModel::dialogParent() const
{
  if (QWidget *active = QApplication::activeWindow()) {
    if (qobject_cast<QMainWindow *>(active)) {
      return active;
    }
  }
  for (QWidget *widget : QApplication::topLevelWidgets()) {
    if (auto *mainWindow = qobject_cast<QMainWindow *>(widget)) {
      if (mainWindow->isVisible()) {
        return mainWindow;
      }
    }
  }
  return nullptr;
}

void InputNodeModel::refreshPathEdit()
{
  if (!m_pathEdit) {
    return;
  }
  m_pathEdit->setText(m_sourcePath);
  m_pathEdit->setToolTip(m_sourcePath);
}

void InputNodeModel::emitCurrentFrame()
{
  if (m_sequence.isEmpty()) {
    Q_EMIT dataInvalidated(0);
    return;
  }
  Q_EMIT dataUpdated(0);
}
