#pragma once

#include "NrOfxIncludes.h"

#include <QString>

#include <cstdarg>
#include <string>

class NrEffectInstance;

class NrOfxHost final : public OFX::Host::ImageEffect::Host
{
public:
  NrOfxHost();

  QString lastMessage() const { return m_lastMessage; }
  void clearLastMessage() { m_lastMessage.clear(); }

  OFX::Host::ImageEffect::Instance *newInstance(void *clientData,
                                                OFX::Host::ImageEffect::ImageEffectPlugin *plugin,
                                                OFX::Host::ImageEffect::Descriptor &desc,
                                                std::string const &context) override;

  OFX::Host::ImageEffect::Descriptor *makeDescriptor(OFX::Host::ImageEffect::ImageEffectPlugin *plugin) override;
  OFX::Host::ImageEffect::Descriptor *makeDescriptor(OFX::Host::ImageEffect::Descriptor const &rootContext,
                                                     OFX::Host::ImageEffect::ImageEffectPlugin *plugin) override;
  OFX::Host::ImageEffect::Descriptor *makeDescriptor(std::string const &bundlePath,
                                                     OFX::Host::ImageEffect::ImageEffectPlugin *plugin) override;

  OfxStatus vmessage(char const *type, char const *id, char const *format, va_list args) override;
  OfxStatus setPersistentMessage(char const *type, char const *id, char const *format, va_list args) override;
  OfxStatus clearPersistentMessage() override;

private:
  QString m_lastMessage;
};
