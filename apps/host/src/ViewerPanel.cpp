#include "ViewerPanel.h"

#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

class ImageView : public QWidget
{
public:
  explicit ImageView(QWidget *parent = nullptr)
    : QWidget(parent)
  {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    setMinimumSize(160, 90);
  }

  void setImage(QImage image)
  {
    m_image = std::move(image);
    update();
  }

  void setPlaceholder(QString text)
  {
    m_placeholder = std::move(text);
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(48, 48, 48));
    if (m_image.isNull()) {
      painter.setPen(QColor(190, 190, 190));
      painter.drawText(rect().adjusted(12, 12, -12, -12),
                       Qt::AlignCenter | Qt::TextWordWrap,
                       m_placeholder.isEmpty() ? QStringLiteral("Kein Bild") : m_placeholder);
      return;
    }

    QSize const fitted = m_image.size().scaled(size(), Qt::KeepAspectRatio);
    QRect target(QPoint(0, 0), fitted);
    target.moveCenter(rect().center());
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(target, m_image);
  }

private:
  QImage m_image;
  QString m_placeholder;
};

ViewerPanel::ViewerPanel(QWidget *parent)
  : QWidget(parent)
{
  auto *layout = new QVBoxLayout(this);

  m_caption = new QLabel(QStringLiteral("Viewer"));
  m_caption->setAlignment(Qt::AlignCenter);
  m_caption->setWordWrap(true);

  m_imageView = new ImageView;

  auto *timeline = new QWidget;
  auto *timelineLayout = new QHBoxLayout(timeline);
  timelineLayout->setContentsMargins(0, 0, 0, 0);
  m_slider = new QSlider(Qt::Horizontal);
  m_spin = new QSpinBox;
  m_spin->setMinimumWidth(72);
  timelineLayout->addWidget(m_slider, 1);
  timelineLayout->addWidget(m_spin);

  auto *transport = new QWidget;
  auto *transportLayout = new QHBoxLayout(transport);
  transportLayout->setContentsMargins(0, 0, 0, 0);
  m_prevButton = new QPushButton(QStringLiteral("Previous"));
  m_playButton = new QPushButton(QStringLiteral("Play"));
  m_stopButton = new QPushButton(QStringLiteral("Stop"));
  m_nextButton = new QPushButton(QStringLiteral("Next"));
  m_prevButton->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
  m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
  m_stopButton->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
  m_nextButton->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));
  transportLayout->addStretch(1);
  transportLayout->addWidget(m_prevButton);
  transportLayout->addWidget(m_playButton);
  transportLayout->addWidget(m_stopButton);
  transportLayout->addWidget(m_nextButton);
  transportLayout->addStretch(1);

  m_playTimer = new QTimer(this);
  m_playTimer->setInterval(1000 / 24);

  m_info = new QLabel(QStringLiteral("Kein Bild"));
  m_info->setAlignment(Qt::AlignCenter);
  m_info->setWordWrap(true);

  layout->addWidget(m_caption);
  layout->addWidget(m_imageView, 1);
  layout->addWidget(timeline);
  layout->addWidget(transport);
  layout->addWidget(m_info);

  connect(m_slider, &QSlider::valueChanged, this, [this](int value) {
    m_spin->blockSignals(true);
    m_spin->setValue(value);
    m_spin->blockSignals(false);
    Q_EMIT frameMoved(value);
  });
  connect(m_spin, &QSpinBox::valueChanged, this, [this](int value) {
    m_slider->blockSignals(true);
    m_slider->setValue(value);
    m_slider->blockSignals(false);
    Q_EMIT frameMoved(value);
  });
  connect(m_playButton, &QPushButton::clicked, this, &ViewerPanel::play);
  connect(m_stopButton, &QPushButton::clicked, this, &ViewerPanel::stopPlayback);
  connect(m_nextButton, &QPushButton::clicked, this, &ViewerPanel::stepNext);
  connect(m_prevButton, &QPushButton::clicked, this, &ViewerPanel::stepPrevious);
  connect(m_playTimer, &QTimer::timeout, this, &ViewerPanel::stepNext);

  setFrameRange(0);
  setImage({});
  setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
}

QSize ViewerPanel::sizeHint() const
{
  return QSize(400, 640);
}

QSize ViewerPanel::minimumSizeHint() const
{
  return QSize(280, 240);
}

void ViewerPanel::setImage(QImage const &image)
{
  m_imageView->setImage(image);
  if (image.isNull()) {
    m_info->setText(QStringLiteral("Kein Bild"));
  } else {
    m_info->setText(QStringLiteral("%1 × %2").arg(image.width()).arg(image.height()));
    m_imageView->setPlaceholder({});
  }
}

void ViewerPanel::setPlaceholder(QString const &text)
{
  m_imageView->setPlaceholder(text);
}

void ViewerPanel::setSource(QString const &source)
{
  m_caption->setText(source.isEmpty() ? QStringLiteral("Viewer") : source);
}

void ViewerPanel::setFrameRange(int frameCount)
{
  m_frameCount = std::max(0, frameCount);
  int const maxFrame = std::max(0, m_frameCount - 1);
  m_slider->setRange(0, maxFrame);
  m_spin->setRange(0, maxFrame);
  updateTransportEnabled();
  if (m_frameCount < 2) {
    stopPlayback();
  }
}

void ViewerPanel::setFrame(int frame)
{
  m_slider->blockSignals(true);
  m_spin->blockSignals(true);
  m_slider->setValue(frame);
  m_spin->setValue(frame);
  m_slider->blockSignals(false);
  m_spin->blockSignals(false);
}

int ViewerPanel::frame() const
{
  return m_slider->value();
}

void ViewerPanel::stopPlayback()
{
  m_playTimer->stop();
}

void ViewerPanel::play()
{
  if (m_frameCount < 2) {
    return;
  }
  m_playTimer->start();
}

void ViewerPanel::stepNext()
{
  if (m_frameCount <= 0) {
    return;
  }
  int next = m_slider->value() + 1;
  if (next >= m_frameCount) {
    next = 0;
  }
  applyFrame(next);
}

void ViewerPanel::stepPrevious()
{
  if (m_frameCount <= 0) {
    return;
  }
  int previous = m_slider->value() - 1;
  if (previous < 0) {
    previous = m_frameCount - 1;
  }
  applyFrame(previous);
}

void ViewerPanel::applyFrame(int frame)
{
  setFrame(frame);
  Q_EMIT frameMoved(frame);
}

void ViewerPanel::updateTransportEnabled()
{
  bool const hasFrames = m_frameCount > 0;
  bool const canPlay = m_frameCount > 1;
  m_slider->setEnabled(canPlay);
  m_spin->setEnabled(hasFrames);
  m_playButton->setEnabled(canPlay);
  m_stopButton->setEnabled(canPlay);
  m_prevButton->setEnabled(hasFrames);
  m_nextButton->setEnabled(hasFrames);
}
