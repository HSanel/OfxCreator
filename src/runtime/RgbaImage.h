#pragma once

#include <QByteArray>
#include <QImage>

#include <cstring>

struct RgbaImage
{
  int width = 0;
  int height = 0;
  QByteArray pixels;

  int pixelCount() const { return width * height; }
  int byteCount() const { return pixelCount() * 4; }
  bool isNull() const { return width <= 0 || height <= 0 || pixels.size() < byteCount(); }
};

inline RgbaImage rgbaFromQImage(QImage const &image)
{
  RgbaImage out;
  if (image.isNull()) {
    return out;
  }
  QImage converted = image.convertToFormat(QImage::Format_RGBA8888);
  out.width = converted.width();
  out.height = converted.height();
  out.pixels.resize(out.byteCount());
  for (int y = 0; y < out.height; ++y) {
    std::memcpy(out.pixels.data() + y * out.width * 4, converted.constScanLine(y), std::size_t(out.width * 4));
  }
  return out;
}

inline QImage qImageFromRgba(RgbaImage const &image)
{
  if (image.isNull()) {
    return {};
  }
  QImage out(image.width, image.height, QImage::Format_RGBA8888);
  for (int y = 0; y < image.height; ++y) {
    std::memcpy(out.scanLine(y), image.pixels.constData() + y * image.width * 4, std::size_t(image.width * 4));
  }
  return out;
}
