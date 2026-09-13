#include "OfxSdkProbe.h"

#include <ofxCore.h>
#include <ofxGPURender.h>

QString ofxSdkVersionLabel()
{
  return QStringLiteral("OpenFX SDK 1.5.1 · %1 · CUDA: %2")
    .arg(QLatin1String(kOfxPropAPIVersion))
    .arg(QLatin1String(kOfxImageEffectPropCudaRenderSupported));
}
