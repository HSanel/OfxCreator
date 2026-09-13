#pragma once

#include <QImage>
#include <QWidget>

class ImageView;
class QSlider;
class QSpinBox;
class QLabel;

class ViewerPanel : public QWidget
{
  Q_OBJECT

public:
  explicit ViewerPanel(QWidget *parent = nullptr);

  void setImage(QImage const &image);
  void setSource(QString const &source);
  void setFrameRange(int frameCount);
  void setFrame(int frame);
  int frame() const;

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

Q_SIGNALS:
  void frameMoved(int frame);

private:
  ImageView *m_imageView = nullptr;
  QLabel *m_caption = nullptr;
  QSlider *m_slider = nullptr;
  QSpinBox *m_spin = nullptr;
  QLabel *m_info = nullptr;
};
