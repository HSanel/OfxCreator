#pragma once

#include <QtNodes/NodeData>

#include <QImage>

class ImageData : public QtNodes::NodeData
{
public:
  ImageData() = default;
  explicit ImageData(QImage image, int frameIndex = 0)
    : m_image(std::move(image))
    , m_frameIndex(frameIndex)
  {
  }

  QtNodes::NodeDataType type() const override
  {
    return {QStringLiteral("image"), QStringLiteral("Image")};
  }

  QImage const &image() const { return m_image; }
  int frameIndex() const { return m_frameIndex; }
  bool isNull() const { return m_image.isNull(); }

private:
  QImage m_image;
  int m_frameIndex = 0;
};
