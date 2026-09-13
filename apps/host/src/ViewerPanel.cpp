#include "ViewerPanel.h"

#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QSlider>
#include <QSpinBox>
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

protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(48, 48, 48));
    if (m_image.isNull() || width() < 2 || height() < 2) {
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

  m_info = new QLabel(QStringLiteral("Kein Bild"));
  m_info->setAlignment(Qt::AlignCenter);
  m_info->setWordWrap(true);

  layout->addWidget(m_caption);
  layout->addWidget(m_imageView, 1);
  layout->addWidget(timeline);
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
  }
}

void ViewerPanel::setSource(QString const &source)
{
  m_caption->setText(source.isEmpty() ? QStringLiteral("Viewer") : source);
}

void ViewerPanel::setFrameRange(int frameCount)
{
  int const maxFrame = std::max(0, frameCount - 1);
  m_slider->setEnabled(frameCount > 1);
  m_spin->setEnabled(frameCount > 0);
  m_slider->setRange(0, maxFrame);
  m_spin->setRange(0, maxFrame);
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
