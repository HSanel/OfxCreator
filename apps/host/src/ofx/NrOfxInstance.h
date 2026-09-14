#pragma once

#include "NrOfxIncludes.h"

#include <QImage>
#include <QString>

#include <cstdarg>
#include <string>
#include <vector>

struct NrOfxFrameState
{
  QImage source;
  QString lastMessage;
  int width = 1;
  int height = 1;
  int frame = 0;
  int frameCount = 1;
};

class NrEffectInstance;

class NrImage final : public OFX::Host::ImageEffect::Image
{
public:
  NrImage(OFX::Host::ImageEffect::ClipInstance &clip, int width, int height, QImage const &source);
  ~NrImage() override = default;

  unsigned char *data() { return m_bytes.data(); }
  unsigned char const *data() const { return m_bytes.data(); }
  int width() const { return m_width; }
  int height() const { return m_height; }

private:
  int m_width = 0;
  int m_height = 0;
  std::vector<unsigned char> m_bytes;
};

class NrClipInstance final : public OFX::Host::ImageEffect::ClipInstance
{
public:
  NrClipInstance(NrEffectInstance *effect, OFX::Host::ImageEffect::ClipDescriptor *desc);
  ~NrClipInstance() override;

  NrImage *outputImage() { return m_outputImage; }

  std::string const &getUnmappedBitDepth() const override;
  std::string const &getUnmappedComponents() const override;
  std::string const &getPremult() const override;
  double getAspectRatio() const override;
  double getFrameRate() const override;
  void getFrameRange(double &startFrame, double &endFrame) const override;
  std::string const &getFieldOrder() const override;
  bool getConnected() const override;
  double getUnmappedFrameRate() const override;
  void getUnmappedFrameRange(double &unmappedStartFrame, double &unmappedEndFrame) const override;
  bool getContinuousSamples() const override;
  OFX::Host::ImageEffect::Image *getImage(OfxTime time, OfxRectD const *optionalBounds) override;
  OfxRectD getRegionOfDefinition(OfxTime time) const override;

private:
  NrEffectInstance *m_effect = nullptr;
  std::string m_name;
  NrImage *m_outputImage = nullptr;
};

class NrEffectInstance final : public OFX::Host::ImageEffect::Instance
{
public:
  NrEffectInstance(OFX::Host::ImageEffect::ImageEffectPlugin *plugin,
                   OFX::Host::ImageEffect::Descriptor &desc,
                   std::string const &context,
                   NrOfxFrameState *state);

  NrOfxFrameState *state() const { return m_state; }

  std::string const &getDefaultOutputFielding() const override;
  OFX::Host::ImageEffect::ClipInstance *newClipInstance(OFX::Host::ImageEffect::Instance *plugin,
                                                        OFX::Host::ImageEffect::ClipDescriptor *descriptor,
                                                        int index) override;
  OfxStatus vmessage(char const *type, char const *id, char const *format, va_list args) override;
  OfxStatus setPersistentMessage(char const *type, char const *id, char const *format, va_list args) override;
  OfxStatus clearPersistentMessage() override;
  void getProjectSize(double &xSize, double &ySize) const override;
  void getProjectOffset(double &xOffset, double &yOffset) const override;
  void getProjectExtent(double &xSize, double &ySize) const override;
  double getProjectPixelAspectRatio() const override;
  double getEffectDuration() const override;
  double getFrameRate() const override;
  double getFrameRecursive() const override;
  void getRenderScaleRecursive(double &x, double &y) const override;
  OFX::Host::Param::Instance *newParam(std::string const &name, OFX::Host::Param::Descriptor &descriptor) override;
  OfxStatus editBegin(std::string const &name) override;
  OfxStatus editEnd() override;
  void progressStart(std::string const &message, std::string const &messageid) override;
  void progressEnd() override;
  bool progressUpdate(double t) override;
  double timeLineGetTime() override;
  void timeLineGotoTime(double t) override;
  void timeLineGetBounds(double &t1, double &t2) override;

private:
  NrOfxFrameState *m_state = nullptr;
};
