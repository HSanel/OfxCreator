#pragma once

#include <QImage>
#include <QWidget>

class ImageView;
class QSlider;
class QSpinBox;
class QLabel;
class QPushButton;
class QTimer;

class ViewerPanel : public QWidget
{
  Q_OBJECT

public:
  explicit ViewerPanel(QWidget *parent = nullptr);

  void setImage(QImage const &image);
  void setPlaceholder(QString const &text);
  void setSource(QString const &source);
  void setFrameRange(int frameCount);
  void setFrame(int frame);
  int frame() const;
  void stopPlayback();

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

Q_SIGNALS:
  void frameMoved(int frame);

private:
  void play();
  void stepNext();
  void stepPrevious();
  void applyFrame(int frame);
  void updateTransportEnabled();

  ImageView *m_imageView = nullptr;
  QLabel *m_caption = nullptr;
  QSlider *m_slider = nullptr;
  QSpinBox *m_spin = nullptr;
  QLabel *m_info = nullptr;
  QPushButton *m_playButton = nullptr;
  QPushButton *m_stopButton = nullptr;
  QPushButton *m_prevButton = nullptr;
  QPushButton *m_nextButton = nullptr;
  QTimer *m_playTimer = nullptr;
  int m_frameCount = 0;
};
