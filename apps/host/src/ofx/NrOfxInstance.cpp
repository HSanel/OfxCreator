#include "NrOfxInstance.h"

#include <QString>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

QImage toRgba8888(QImage const &image)
{
  if (image.isNull()) {
    return {};
  }
  if (image.format() == QImage::Format_RGBA8888) {
    return image;
  }
  return image.convertToFormat(QImage::Format_RGBA8888);
}

} // namespace

NrImage::NrImage(OFX::Host::ImageEffect::ClipInstance &clip, int width, int height, QImage const &source)
  : OFX::Host::ImageEffect::Image(clip)
  , m_width(std::max(1, width))
  , m_height(std::max(1, height))
{
  m_bytes.resize(static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * 4u, 0);
  QImage const rgba = toRgba8888(source);
  if (!rgba.isNull()) {
    int const copyW = std::min(m_width, rgba.width());
    int const copyH = std::min(m_height, rgba.height());
    for (int y = 0; y < copyH; ++y) {
      unsigned char const *src = rgba.constScanLine(y);
      unsigned char *dst = m_bytes.data() + static_cast<size_t>(y) * static_cast<size_t>(m_width) * 4u;
      std::memcpy(dst, src, static_cast<size_t>(copyW) * 4u);
    }
  }

  setDoubleProperty(kOfxImageEffectPropRenderScale, 1.0, 0);
  setDoubleProperty(kOfxImageEffectPropRenderScale, 1.0, 1);
  setPointerProperty(kOfxImagePropData, m_bytes.data());
  setIntProperty(kOfxImagePropBounds, 0, 0);
  setIntProperty(kOfxImagePropBounds, 0, 1);
  setIntProperty(kOfxImagePropBounds, m_width, 2);
  setIntProperty(kOfxImagePropBounds, m_height, 3);
  setIntProperty(kOfxImagePropRegionOfDefinition, 0, 0);
  setIntProperty(kOfxImagePropRegionOfDefinition, 0, 1);
  setIntProperty(kOfxImagePropRegionOfDefinition, m_width, 2);
  setIntProperty(kOfxImagePropRegionOfDefinition, m_height, 3);
  setIntProperty(kOfxImagePropRowBytes, m_width * 4);
  setStringProperty(kOfxImagePropField, kOfxImageFieldNone);
  setStringProperty(kOfxImagePropUniqueIdentifier, "nr-frame");
}

NrClipInstance::NrClipInstance(NrEffectInstance *effect, OFX::Host::ImageEffect::ClipDescriptor *desc)
  : OFX::Host::ImageEffect::ClipInstance(effect, *desc)
  , m_effect(effect)
  , m_name(desc->getName())
{
}

NrClipInstance::~NrClipInstance()
{
  if (m_outputImage) {
    m_outputImage->releaseReference();
    m_outputImage = nullptr;
  }
}

std::string const &NrClipInstance::getUnmappedBitDepth() const
{
  static std::string const v(kOfxBitDepthByte);
  return v;
}

std::string const &NrClipInstance::getUnmappedComponents() const
{
  static std::string const v(kOfxImageComponentRGBA);
  return v;
}

std::string const &NrClipInstance::getPremult() const
{
  static std::string const v(kOfxImageUnPreMultiplied);
  return v;
}

double NrClipInstance::getAspectRatio() const
{
  return 1.0;
}

double NrClipInstance::getFrameRate() const
{
  return 24.0;
}

void NrClipInstance::getFrameRange(double &startFrame, double &endFrame) const
{
  startFrame = 0;
  endFrame = m_effect && m_effect->state() ? m_effect->state()->frameCount : 1;
}

std::string const &NrClipInstance::getFieldOrder() const
{
  static std::string const v(kOfxImageFieldNone);
  return v;
}

bool NrClipInstance::getConnected() const
{
  return true;
}

double NrClipInstance::getUnmappedFrameRate() const
{
  return 24.0;
}

void NrClipInstance::getUnmappedFrameRange(double &unmappedStartFrame, double &unmappedEndFrame) const
{
  getFrameRange(unmappedStartFrame, unmappedEndFrame);
}

bool NrClipInstance::getContinuousSamples() const
{
  return false;
}

OfxRectD NrClipInstance::getRegionOfDefinition(OfxTime) const
{
  OfxRectD rod{};
  int const w = m_effect && m_effect->state() ? m_effect->state()->width : 1;
  int const h = m_effect && m_effect->state() ? m_effect->state()->height : 1;
  rod.x2 = w;
  rod.y2 = h;
  return rod;
}

OFX::Host::ImageEffect::Image *NrClipInstance::getImage(OfxTime, OfxRectD const *)
{
  NrOfxFrameState *state = m_effect ? m_effect->state() : nullptr;
  int const w = state ? state->width : 1;
  int const h = state ? state->height : 1;
  if (m_name == kOfxImageEffectOutputClipName) {
    if (m_outputImage && (m_outputImage->width() != w || m_outputImage->height() != h)) {
      m_outputImage->releaseReference();
      m_outputImage = nullptr;
    }
    if (!m_outputImage) {
      m_outputImage = new NrImage(*this, w, h, QImage());
    }
    m_outputImage->addReference();
    return m_outputImage;
  }
  return new NrImage(*this, w, h, state ? state->source : QImage());
}

NrEffectInstance::NrEffectInstance(OFX::Host::ImageEffect::ImageEffectPlugin *plugin,
                                   OFX::Host::ImageEffect::Descriptor &desc,
                                   std::string const &context,
                                   NrOfxFrameState *state)
  : OFX::Host::ImageEffect::Instance(plugin, desc, context, false)
  , m_state(state)
{
}

std::string const &NrEffectInstance::getDefaultOutputFielding() const
{
  static std::string const v(kOfxImageFieldNone);
  return v;
}

OFX::Host::ImageEffect::ClipInstance *NrEffectInstance::newClipInstance(OFX::Host::ImageEffect::Instance *,
                                                                        OFX::Host::ImageEffect::ClipDescriptor *descriptor,
                                                                        int)
{
  return new NrClipInstance(this, descriptor);
}

OfxStatus NrEffectInstance::vmessage(char const *, char const *, char const *format, va_list args)
{
  if (format) {
    QString const text = QString::vasprintf(format, args);
    if (m_state) {
      m_state->lastMessage = text;
    }
    std::fprintf(stderr, "%s\n", qPrintable(text));
  }
  return kOfxStatOK;
}

OfxStatus NrEffectInstance::setPersistentMessage(char const *type, char const *id, char const *format, va_list args)
{
  return vmessage(type, id, format, args);
}

OfxStatus NrEffectInstance::clearPersistentMessage()
{
  if (m_state) {
    m_state->lastMessage.clear();
  }
  return kOfxStatOK;
}

void NrEffectInstance::getProjectSize(double &xSize, double &ySize) const
{
  xSize = m_state ? m_state->width : 1;
  ySize = m_state ? m_state->height : 1;
}

void NrEffectInstance::getProjectOffset(double &xOffset, double &yOffset) const
{
  xOffset = 0;
  yOffset = 0;
}

void NrEffectInstance::getProjectExtent(double &xSize, double &ySize) const
{
  getProjectSize(xSize, ySize);
}

double NrEffectInstance::getProjectPixelAspectRatio() const
{
  return 1.0;
}

double NrEffectInstance::getEffectDuration() const
{
  return m_state ? m_state->frameCount : 1;
}

double NrEffectInstance::getFrameRate() const
{
  return 24.0;
}

double NrEffectInstance::getFrameRecursive() const
{
  return m_state ? m_state->frame : 0;
}

void NrEffectInstance::getRenderScaleRecursive(double &x, double &y) const
{
  x = y = 1.0;
}

OFX::Host::Param::Instance *NrEffectInstance::newParam(std::string const &, OFX::Host::Param::Descriptor &descriptor)
{
  if (descriptor.getType() == kOfxParamTypeGroup) {
    return new OFX::Host::Param::GroupInstance(descriptor, this);
  }
  if (descriptor.getType() == kOfxParamTypePage) {
    return new OFX::Host::Param::PageInstance(descriptor, this);
  }
  return nullptr;
}

OfxStatus NrEffectInstance::editBegin(std::string const &)
{
  return kOfxStatErrMissingHostFeature;
}

OfxStatus NrEffectInstance::editEnd()
{
  return kOfxStatErrMissingHostFeature;
}

void NrEffectInstance::progressStart(std::string const &, std::string const &) {}

void NrEffectInstance::progressEnd() {}

bool NrEffectInstance::progressUpdate(double)
{
  return true;
}

double NrEffectInstance::timeLineGetTime()
{
  return m_state ? m_state->frame : 0;
}

void NrEffectInstance::timeLineGotoTime(double) {}

void NrEffectInstance::timeLineGetBounds(double &t1, double &t2)
{
  t1 = 0;
  t2 = m_state ? m_state->frameCount : 1;
}
