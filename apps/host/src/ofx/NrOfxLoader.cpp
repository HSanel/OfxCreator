#include "NrOfxLoader.h"

#include "NrOfxHost.h"
#include "NrOfxInstance.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cstring>
#include <map>

namespace {

QString scanDirectoryFromPath(QString path)
{
  QFileInfo info(path);
  if (info.isDir()) {
    if (path.endsWith(QStringLiteral(".ofx.bundle"), Qt::CaseInsensitive)) {
      return info.absolutePath();
    }
    QDir dir(info.absoluteFilePath());
    QStringList const bundles = dir.entryList({QStringLiteral("*.ofx.bundle")}, QDir::Dirs);
    if (!bundles.isEmpty()) {
      return info.absoluteFilePath();
    }
    if (dir.dirName().compare(QStringLiteral("Win64"), Qt::CaseInsensitive) == 0
        || dir.dirName().compare(QStringLiteral("win64"), Qt::CaseInsensitive) == 0) {
      return QFileInfo(dir.filePath(QStringLiteral("../../.."))).absoluteFilePath();
    }
  }
  if (info.suffix().compare(QStringLiteral("ofx"), Qt::CaseInsensitive) == 0) {
    return QFileInfo(info.dir().filePath(QStringLiteral("../../.."))).absoluteFilePath();
  }
  return info.absolutePath();
}

} // namespace

struct NrOfxLoader::Impl
{
  NrOfxHost host;
  std::unique_ptr<OFX::Host::ImageEffect::PluginCache> effectCache;
  std::unique_ptr<OFX::Host::ImageEffect::Instance> instance;
  NrOfxFrameState state;
  OFX::Host::ImageEffect::ImageEffectPlugin *plugin = nullptr;
};

NrOfxLoader::NrOfxLoader()
  : m_impl(std::make_unique<Impl>())
{
}

NrOfxLoader::~NrOfxLoader()
{
  unload();
}

namespace {

int g_loadedBundles = 0;

} // namespace

void NrOfxLoader::unload()
{
  bool const wasLoaded = m_impl->instance != nullptr;
  m_impl->instance.reset();
  m_impl->plugin = nullptr;
  m_impl->effectCache.reset();
  m_pluginId.clear();
  if (wasLoaded && m_countsLoaded) {
    --g_loadedBundles;
    m_countsLoaded = false;
    if (g_loadedBundles <= 0) {
      OFX::Host::PluginCache::clearPluginCache();
      g_loadedBundles = 0;
    }
  }
}

bool NrOfxLoader::isLoaded() const
{
  return m_impl->instance != nullptr;
}

bool NrOfxLoader::loadBundle(QString const &path, QString *error)
{
  unload();

  QString const scanDir = QDir::toNativeSeparators(scanDirectoryFromPath(path));
  if (scanDir.isEmpty() || !QDir(scanDir).exists()) {
    if (error) {
      *error = QStringLiteral("Kein OFX-Bundle unter %1 gefunden.").arg(path);
    }
    return false;
  }

  OFX::Host::PluginCache *cache = OFX::Host::PluginCache::getPluginCache();
  cache->setCacheVersion("nrHostV1");
  cache->prependFileToPath(scanDir.toStdString(), true);

  m_impl->effectCache = std::make_unique<OFX::Host::ImageEffect::PluginCache>(m_impl->host);
  m_impl->effectCache->registerInCache(*cache);
  cache->scanPluginFiles();

  auto const &plugins = m_impl->effectCache->getPlugins();
  if (plugins.empty()) {
    if (error) {
      *error = QStringLiteral("Kein Image-Effect-Plugin in %1.").arg(scanDir);
    }
    unload();
    return false;
  }

  QFileInfo const wanted(path);
  m_impl->plugin = nullptr;
  for (OFX::Host::ImageEffect::ImageEffectPlugin *candidate : plugins) {
    if (!candidate || !candidate->getBinary()) {
      continue;
    }
    QFileInfo const binary(QString::fromStdString(candidate->getBinary()->getFilePath()));
    if (!wanted.suffix().compare(QStringLiteral("ofx"), Qt::CaseInsensitive)
        && QString::compare(binary.absoluteFilePath(), wanted.absoluteFilePath(), Qt::CaseInsensitive) == 0) {
      m_impl->plugin = candidate;
      break;
    }
  }
  if (!m_impl->plugin) {
    for (OFX::Host::ImageEffect::ImageEffectPlugin *candidate : plugins) {
      if (QString::fromStdString(candidate->getIdentifier()).startsWith(QStringLiteral("de.nr.ofx."))) {
        m_impl->plugin = candidate;
        break;
      }
    }
  }
  if (!m_impl->plugin) {
    m_impl->plugin = plugins.front();
  }
  m_pluginId = QString::fromStdString(m_impl->plugin->getIdentifier());

  m_impl->state.width = 1;
  m_impl->state.height = 1;
  m_impl->state.frameCount = 1;
  OFX::Host::ImageEffect::Instance *raw =
    m_impl->plugin->createInstance(kOfxImageEffectContextFilter, &m_impl->state);
  if (!raw) {
    if (error) {
      *error = QStringLiteral("Filter-Instanz fehlgeschlagen (%1).").arg(m_pluginId);
    }
    unload();
    return false;
  }
  m_impl->instance.reset(raw);

  OfxStatus const created = m_impl->instance->createInstanceAction();
  if (created != kOfxStatOK && created != kOfxStatReplyDefault) {
    if (error) {
      *error = QStringLiteral("createInstanceAction fehlgeschlagen.");
    }
    unload();
    return false;
  }
  if (!m_impl->instance->getClipPreferences()) {
    if (error) {
      *error = QStringLiteral("getClipPreferences fehlgeschlagen.");
    }
    unload();
    return false;
  }
  m_countsLoaded = true;
  ++g_loadedBundles;
  return true;
}

QImage NrOfxLoader::render(QImage const &source, int frame, int frameCount, QString *error)
{
  if (!m_impl->instance) {
    if (error) {
      *error = QStringLiteral("Kein OFX-Plugin geladen.");
    }
    return {};
  }
  if (source.isNull()) {
    if (error) {
      *error = QStringLiteral("Kein Quellbild (Input-Sequenz).");
    }
    return {};
  }

  QImage rgba = source.convertToFormat(QImage::Format_RGBA8888).copy();
  m_impl->host.clearLastMessage();
  m_impl->state.lastMessage.clear();
  m_impl->state.source = rgba;
  m_impl->state.width = rgba.width();
  m_impl->state.height = rgba.height();
  m_impl->state.frame = std::max(0, frame);
  m_impl->state.frameCount = std::max(1, frameCount);

  auto fail = [&](QString const &text) {
    if (error) {
      QString extra = m_impl->state.lastMessage;
      if (extra.isEmpty()) {
        extra = m_impl->host.lastMessage();
      }
      *error = extra.isEmpty() ? text : text + QStringLiteral("\n") + extra;
    }
    return QImage();
  };

  try {
    if (!m_impl->instance->getClipPreferences()) {
      return fail(QStringLiteral("getClipPreferences fehlgeschlagen."));
    }
  } catch (...) {
    return fail(QStringLiteral("getClipPreferences Ausnahme."));
  }

  if (!m_impl->instance->getClip(kOfxImageEffectSimpleSourceClipName)
      || !m_impl->instance->getClip(kOfxImageEffectOutputClipName)) {
    return fail(QStringLiteral("Plugin hat keine Source/Output-Clips (DescribeInContext)."));
  }

  OfxPointD scale{1.0, 1.0};
  OfxRectI window{0, 0, m_impl->state.width, m_impl->state.height};
  OfxTime const time = m_impl->state.frame;

  OfxStatus stat = m_impl->instance->beginRenderAction(time, time, 1.0, false, scale, true, false);
  if (stat != kOfxStatOK && stat != kOfxStatReplyDefault) {
    return fail(QStringLiteral("beginRenderAction fehlgeschlagen."));
  }

  OfxRectD roi{0, 0, static_cast<double>(m_impl->state.width), static_cast<double>(m_impl->state.height)};
  std::map<OFX::Host::ImageEffect::ClipInstance *, OfxRectD> rois;
  try {
    stat = m_impl->instance->getRegionOfInterestAction(time, scale, roi, rois);
    (void)stat;
    stat = m_impl->instance->renderAction(time, kOfxImageFieldNone, window, scale, true, false, false);
  } catch (...) {
    m_impl->instance->endRenderAction(time, time, 1.0, false, scale, true, false);
    return fail(QStringLiteral("renderAction Ausnahme."));
  }
  m_impl->instance->endRenderAction(time, time, 1.0, false, scale, true, false);
  if (stat != kOfxStatOK) {
    return fail(QStringLiteral("renderAction fehlgeschlagen (%1). CUDA/OpenCL-Plugins brauchen NVRTC/OpenCL — neu exportieren nach dem Host-Update.")
                    .arg(stat));
  }

  auto *outputClip = dynamic_cast<NrClipInstance *>(m_impl->instance->getClip(kOfxImageEffectOutputClipName));
  NrImage *out = outputClip ? outputClip->outputImage() : nullptr;
  if (!out || !out->data()) {
    if (error) {
      *error = QStringLiteral("Kein Output-Bild vom Plugin.");
    }
    return {};
  }

  QImage result(out->width(), out->height(), QImage::Format_RGBA8888);
  for (int y = 0; y < out->height(); ++y) {
    unsigned char const *src = out->data() + static_cast<size_t>(y) * static_cast<size_t>(out->width()) * 4u;
    std::memcpy(result.scanLine(y), src, static_cast<size_t>(out->width()) * 4u);
  }
  return result;
}
