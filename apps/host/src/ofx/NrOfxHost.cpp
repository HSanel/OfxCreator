#include "NrOfxHost.h"
#include "NrOfxInstance.h"

#include <QString>

#include <cstdio>
#include <cstring>

NrOfxHost::NrOfxHost()
{
  _properties.setIntProperty(kOfxPropAPIVersion, 1, 0);
  _properties.setIntProperty(kOfxPropAPIVersion, 4, 1);
  _properties.setStringProperty(kOfxPropName, "de.nr.ofxhost");
  _properties.setStringProperty(kOfxPropLabel, "NR OFX Host");
  _properties.setIntProperty(kOfxPropVersion, 0, 0);
  _properties.setIntProperty(kOfxPropVersion, 5, 1);
  _properties.setStringProperty(kOfxPropVersionLabel, "0.5");
  _properties.setIntProperty(kOfxImageEffectHostPropIsBackground, 0);
  _properties.setIntProperty(kOfxImageEffectPropSupportsOverlays, 0);
  _properties.setIntProperty(kOfxImageEffectPropSupportsMultiResolution, 0);
  _properties.setIntProperty(kOfxImageEffectPropSupportsTiles, 0);
  _properties.setIntProperty(kOfxImageEffectPropTemporalClipAccess, 0);
  _properties.setStringProperty(kOfxImageEffectPropSupportedComponents, kOfxImageComponentRGBA, 0);
  _properties.setStringProperty(kOfxImageEffectPropSupportedPixelDepths, kOfxBitDepthByte, 0);
  _properties.setStringProperty(kOfxImageEffectPropSupportedContexts, kOfxImageEffectContextFilter, 0);
  _properties.setIntProperty(kOfxImageEffectPropSupportsMultipleClipDepths, 0);
  _properties.setIntProperty(kOfxImageEffectPropSupportsMultipleClipPARs, 0);
  _properties.setIntProperty(kOfxImageEffectPropSetableFrameRate, 0);
  _properties.setIntProperty(kOfxImageEffectPropSetableFielding, 0);
  _properties.setIntProperty(kOfxParamHostPropSupportsCustomInteract, 0);
  _properties.setIntProperty(kOfxParamHostPropSupportsStringAnimation, 0);
  _properties.setIntProperty(kOfxParamHostPropSupportsChoiceAnimation, 0);
  _properties.setIntProperty(kOfxParamHostPropSupportsBooleanAnimation, 0);
  _properties.setIntProperty(kOfxParamHostPropSupportsCustomAnimation, 0);
  _properties.setIntProperty(kOfxParamHostPropMaxParameters, -1);
  _properties.setIntProperty(kOfxParamHostPropMaxPages, 0);
  _properties.setIntProperty(kOfxParamHostPropPageRowColumnCount, 0, 0);
  _properties.setIntProperty(kOfxParamHostPropPageRowColumnCount, 0, 1);
}

OFX::Host::ImageEffect::Instance *NrOfxHost::newInstance(void *clientData,
                                                         OFX::Host::ImageEffect::ImageEffectPlugin *plugin,
                                                         OFX::Host::ImageEffect::Descriptor &desc,
                                                         std::string const &context)
{
  return new NrEffectInstance(plugin, desc, context, static_cast<NrOfxFrameState *>(clientData));
}

OFX::Host::ImageEffect::Descriptor *NrOfxHost::makeDescriptor(OFX::Host::ImageEffect::ImageEffectPlugin *plugin)
{
  return new OFX::Host::ImageEffect::Descriptor(static_cast<OFX::Host::Plugin *>(plugin));
}

OFX::Host::ImageEffect::Descriptor *NrOfxHost::makeDescriptor(OFX::Host::ImageEffect::Descriptor const &rootContext,
                                                              OFX::Host::ImageEffect::ImageEffectPlugin *plugin)
{
  return new OFX::Host::ImageEffect::Descriptor(rootContext, static_cast<OFX::Host::Plugin *>(plugin));
}

OFX::Host::ImageEffect::Descriptor *NrOfxHost::makeDescriptor(std::string const &bundlePath,
                                                              OFX::Host::ImageEffect::ImageEffectPlugin *plugin)
{
  return new OFX::Host::ImageEffect::Descriptor(bundlePath, static_cast<OFX::Host::Plugin *>(plugin));
}

OfxStatus NrOfxHost::vmessage(char const *type, char const * /*id*/, char const *format, va_list args)
{
  if (format) {
    m_lastMessage = QString::vasprintf(format, args);
    std::fprintf(stderr, "%s\n", qPrintable(m_lastMessage));
  }
  if (type && std::strcmp(type, kOfxMessageQuestion) == 0) {
    return kOfxStatReplyYes;
  }
  return kOfxStatOK;
}

OfxStatus NrOfxHost::setPersistentMessage(char const *type, char const *id, char const *format, va_list args)
{
  return vmessage(type, id, format, args);
}

OfxStatus NrOfxHost::clearPersistentMessage()
{
  return kOfxStatOK;
}
