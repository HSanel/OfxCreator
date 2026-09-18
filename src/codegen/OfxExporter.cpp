#include "codegen/OfxExporter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QQueue>
#include <QRegularExpression>
#include <QSet>

#ifndef NR_OPENFX_INCLUDE
#  define NR_OPENFX_INCLUDE ""
#endif
#ifndef NR_CODEGEN_DIR
#  define NR_CODEGEN_DIR ""
#endif

namespace {

bool writeText(QString const &path, QString const &text, QString *error)
{
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    if (error) {
      *error = QStringLiteral("Datei nicht schreibbar: %1").arg(path);
    }
    return false;
  }
  if (file.write(text.toUtf8()) < 0) {
    if (error) {
      *error = QStringLiteral("Schreiben fehlgeschlagen: %1").arg(path);
    }
    return false;
  }
  return true;
}

bool copyFile(QString const &from, QString const &to, QString *error)
{
  if (QFile::exists(to)) {
    QFile::remove(to);
  }
  if (!QFile::copy(from, to)) {
    if (error) {
      *error = QStringLiteral("Kopieren fehlgeschlagen: %1 → %2").arg(from, to);
    }
    return false;
  }
  return true;
}

QString sanitizeIdent(QString name)
{
  name = name.trimmed();
  if (name.isEmpty()) {
    name = QStringLiteral("NrFilter");
  }
  name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]+")), QStringLiteral("_"));
  if (name[0].isDigit()) {
    name.prepend(QLatin1Char('_'));
  }
  return name;
}

QString pluginIdFromName(QString const &name)
{
  return QStringLiteral("de.nr.ofx.") + sanitizeIdent(name).toLower();
}

QString cppRaw(QString const &source)
{
  QString body = source;
  body.replace(QStringLiteral(")NRKOFX"), QStringLiteral(") NRKOFX"));
  return QStringLiteral("R\"NRKOFX(%1)NRKOFX\"").arg(body);
}

QString findGxx()
{
  QString const fromEnv = qEnvironmentVariable("NR_CXX");
  if (!fromEnv.isEmpty()) {
    return fromEnv;
  }
  QStringList const candidates = {
    QStringLiteral("C:/Qt/Tools/mingw1310_64/bin/g++.exe"),
    QStringLiteral("C:/Qt/Tools/mingw1120_64/bin/g++.exe"),
    QStringLiteral("C:/mingw64/bin/g++.exe"),
  };
  for (QString const &candidate : candidates) {
    if (QFileInfo::exists(candidate)) {
      return candidate;
    }
  }
  return QStringLiteral("g++");
}

struct ExportStep
{
  NodeKind kind = NodeKind::Unknown;
  QString source;
  QString symbol; // nr_process_N or kGpuSource_N
  int index = 0;
};

bool isKernel(NodeKind kind)
{
  return kind == NodeKind::Cpu || kind == NodeKind::Cuda || kind == NodeKind::OpenCl || kind == NodeKind::Python;
}

QVector<quint64> topoKernelIds(Pipeline const &pipeline)
{
  QHash<quint64, int> indegree;
  QHash<quint64, QVector<quint64>> outgoing;
  QSet<quint64> kernels;
  for (PipelineNode const &node : pipeline.nodes) {
    if (isKernel(node.kind)) {
      kernels.insert(node.id);
      indegree[node.id] = 0;
    }
  }
  for (PipelineConnection const &edge : pipeline.connections) {
    if (!kernels.contains(edge.fromId) || !kernels.contains(edge.toId)) {
      continue;
    }
    outgoing[edge.fromId].push_back(edge.toId);
    indegree[edge.toId] += 1;
  }

  QQueue<quint64> ready;
  for (quint64 id : kernels) {
    if (indegree.value(id) == 0) {
      ready.enqueue(id);
    }
  }
  QVector<quint64> order;
  QSet<quint64> seen;
  while (!ready.isEmpty()) {
    quint64 const id = ready.dequeue();
    if (seen.contains(id)) {
      continue;
    }
    seen.insert(id);
    order.push_back(id);
    for (quint64 next : outgoing.value(id)) {
      indegree[next] -= 1;
      if (indegree[next] <= 0) {
        ready.enqueue(next);
      }
    }
  }
  for (quint64 id : kernels) {
    if (!seen.contains(id)) {
      order.push_back(id);
    }
  }
  return order;
}

QString infoPlist(QString const &executable)
{
  return QStringLiteral(R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>English</string>
	<key>CFBundleExecutable</key>
	<string>%1</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundlePackageType</key>
	<string>BNDL</string>
	<key>CFBundleSignature</key>
	<string>????</string>
	<key>CFBundleVersion</key>
	<string>0.1.0</string>
	<key>CSResourcesFileMapped</key>
	<true/>
</dict>
</plist>
)").arg(executable);
}

QString cmakeLists(QString const &target, QString const &openfxInclude)
{
  QString include = openfxInclude;
  include.replace(QLatin1Char('\\'), QLatin1Char('/'));
  return QStringLiteral(R"(cmake_minimum_required(VERSION 3.24)
project(%1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(%1 MODULE
  plugin.cpp
  gpu_launch.cpp
)

target_include_directories(%1 PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "%2"
)

set_target_properties(%1 PROPERTIES
  PREFIX ""
  SUFFIX ".ofx"
  OUTPUT_NAME "%1"
)

if(MSVC)
  target_compile_definitions(%1 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Resolve: Release-`.ofx` nach %1.ofx.bundle/Contents/Win64/ kopieren,
# Bundle nach C:/Program Files/Common Files/OFX/Plugins/.
)").arg(target, include);
}

QString readme(QString const &name)
{
  return QStringLiteral(
      "OFX-Plugin **%1**, erzeugt aus einer NR-Pipeline.\n\n"
      "## Bundle\n"
      "`%1.ofx.bundle/Contents/Win64/%1.ofx`\n\n"
      "## Kompilieren\n"
      "DaVinci Resolve auf Windows erwartet in der Regel **MSVC**. "
      "MinGW erzeugt ein `.ofx` für den NR-Host-Loader.\n\n"
      "```\n"
      "cmake -S . -B build -G \"Visual Studio 17 2022\" -A x64\n"
      "cmake --build build --config Release\n"
      "```\n\n"
      "Nach dem MSVC-Build die Datei nach `build/Release/%1.ofx` (oder analog) in das Bundle kopieren:\n"
      "`%1.ofx.bundle/Contents/Win64/%1.ofx`\n\n"
      "## Installation in Resolve\n"
      "Bundle nach `C:\\Program Files\\Common Files\\OFX\\Plugins\\` kopieren "
      "(Admin-Rechte). Resolve neu starten.\n\n"
      "Python-Knoten werden nicht nach OFX exportiert.\n"
      "CUDA/OpenCL-Kernel laufen zur Renderzeit über NVRTC bzw. OpenCL.dll (Kernel `process`) "
      "auf **CPU-Hostpuffern**. Das Plugin meldet `CudaRenderSupported`/`OpenCLRenderSupported` = false, "
      "damit Resolve keine Gerätezeiger übergibt. Bitttiefen: Byte, Short, Float (intern 8-Bit-RGBA).\n\n"
      "## Smoke-Test Resolve\n"
      "1. Fusion- oder Color-Seite: Clip + Effekt **NR OFX Host / %1**.\n"
      "2. Filter-Kontext (Source + Output), Timeline Byte und Float prüfen.\n"
      "3. GPU: DaVinci Resolve → Einstellungen → GPU — CUDA bzw. OpenCL. "
      "Kernel müssen trotzdem laufen (intern NVRTC/OpenCL, nicht OFX-GPU-Suite).\n"
      "4. CPU-Invert und CUDA-Invert getrennt exportieren und visuell vergleichen.\n")
      .arg(name);
}

QString pluginCpp(QString const &pluginId, QString const &label, QString const &includes, QString const &gpuSources,
                  QString const &steps)
{
  QString text = QStringLiteral(R"CPP(#include "nr_cpu_abi.h"
#include "gpu_launch.h"

#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ofxImageEffect.h"
#include "ofxMemory.h"
#include "ofxMultiThread.h"
#include "ofxPixels.h"
#include "ofxGPURender.h"

#if defined __APPLE__ || defined __linux__ || defined __FreeBSD__
#  define EXPORT __attribute__((visibility("default")))
#elif defined _WIN32
#  define EXPORT OfxExport
#else
#  error Unsupported OS
#endif

OfxHost *gHost = nullptr;
OfxImageEffectSuiteV1 *gEffectHost = nullptr;
OfxPropertySuiteV1 *gPropHost = nullptr;
OfxMessageSuiteV1 *gMessageHost = nullptr;

static void report(OfxImageEffectHandle instance, char const *text)
{
  if (gMessageHost && gMessageHost->message && text && *text) {
    gMessageHost->message(instance, kOfxMessageError, nullptr, "%s", text);
  }
}

/*NR_CPU_INCLUDES*/
/*NR_GPU_SOURCES*/
enum class PixelDepth { Byte, Short, Float };

static PixelDepth parseDepth(char const *name)
{
  if (name && std::strcmp(name, kOfxBitDepthFloat) == 0) {
    return PixelDepth::Float;
  }
  if (name && std::strcmp(name, kOfxBitDepthShort) == 0) {
    return PixelDepth::Short;
  }
  return PixelDepth::Byte;
}

static int pixelBytes(PixelDepth depth)
{
  switch (depth) {
  case PixelDepth::Float:
    return static_cast<int>(4 * sizeof(float));
  case PixelDepth::Short:
    return static_cast<int>(4 * sizeof(unsigned short));
  default:
    return 4;
  }
}

static unsigned char floatToByte(float v)
{
  if (v <= 0.f) {
    return 0;
  }
  if (v >= 1.f) {
    return 255;
  }
  return static_cast<unsigned char>(v * 255.f + 0.5f);
}

static char *pixelAddress(void *img, OfxRectI rect, int x, int y, int bytesPerLine, int bpp)
{
  if (!img || x < rect.x1 || x >= rect.x2 || y < rect.y1 || y >= rect.y2) {
    return nullptr;
  }
  return static_cast<char *>(img) + (y - rect.y1) * bytesPerLine + (x - rect.x1) * bpp;
}

class NoImageEx {};

static void packRgba(void *src, PixelDepth depth, OfxRectI srcRect, int srcRowBytes, OfxRectI window, unsigned char *dst)
{
  int const width = window.x2 - window.x1;
  int const height = window.y2 - window.y1;
  int const bpp = pixelBytes(depth);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      char *pix = pixelAddress(src, srcRect, window.x1 + x, window.y1 + y, srcRowBytes, bpp);
      unsigned char *out = dst + (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
      if (!pix) {
        out[0] = out[1] = out[2] = out[3] = 0;
        continue;
      }
      if (depth == PixelDepth::Float) {
        auto const *c = reinterpret_cast<float const *>(pix);
        out[0] = floatToByte(c[0]);
        out[1] = floatToByte(c[1]);
        out[2] = floatToByte(c[2]);
        out[3] = floatToByte(c[3]);
      } else if (depth == PixelDepth::Short) {
        auto const *c = reinterpret_cast<unsigned short const *>(pix);
        out[0] = static_cast<unsigned char>(c[0] >> 8);
        out[1] = static_cast<unsigned char>(c[1] >> 8);
        out[2] = static_cast<unsigned char>(c[2] >> 8);
        out[3] = static_cast<unsigned char>(c[3] >> 8);
      } else {
        auto const *c = reinterpret_cast<unsigned char const *>(pix);
        out[0] = c[0];
        out[1] = c[1];
        out[2] = c[2];
        out[3] = c[3];
      }
    }
  }
}

static void unpackRgba(unsigned char const *src, void *dst, PixelDepth depth, OfxRectI dstRect, int dstRowBytes,
                       OfxRectI window)
{
  int const width = window.x2 - window.x1;
  int const height = window.y2 - window.y1;
  int const bpp = pixelBytes(depth);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      char *pix = pixelAddress(dst, dstRect, window.x1 + x, window.y1 + y, dstRowBytes, bpp);
      if (!pix) {
        continue;
      }
      unsigned char const *in =
          src + (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
      if (depth == PixelDepth::Float) {
        auto *c = reinterpret_cast<float *>(pix);
        c[0] = in[0] / 255.f;
        c[1] = in[1] / 255.f;
        c[2] = in[2] / 255.f;
        c[3] = in[3] / 255.f;
      } else if (depth == PixelDepth::Short) {
        auto *c = reinterpret_cast<unsigned short *>(pix);
        c[0] = static_cast<unsigned short>(in[0] * 257);
        c[1] = static_cast<unsigned short>(in[1] * 257);
        c[2] = static_cast<unsigned short>(in[2] * 257);
        c[3] = static_cast<unsigned short>(in[3] * 257);
      } else {
        auto *c = reinterpret_cast<unsigned char *>(pix);
        c[0] = in[0];
        c[1] = in[1];
        c[2] = in[2];
        c[3] = in[3];
      }
    }
  }
}

static OfxStatus render(OfxImageEffectHandle instance, OfxPropertySetHandle inArgs, OfxPropertySetHandle)
{
  OfxTime time = 0;
  OfxRectI renderWindow{};
  OfxStatus status = kOfxStatOK;
  gPropHost->propGetDouble(inArgs, kOfxPropTime, 0, &time);
  gPropHost->propGetIntN(inArgs, kOfxImageEffectPropRenderWindow, 4, &renderWindow.x1);

  OfxImageClipHandle outputClip = nullptr;
  OfxImageClipHandle sourceClip = nullptr;
  if (gEffectHost->clipGetHandle(instance, kOfxImageEffectOutputClipName, &outputClip, nullptr) != kOfxStatOK
      || gEffectHost->clipGetHandle(instance, kOfxImageEffectSimpleSourceClipName, &sourceClip, nullptr) != kOfxStatOK) {
    report(instance, "clipGetHandle Source/Output fehlgeschlagen.");
    return kOfxStatFailed;
  }

  OfxPropertySetHandle outputImg = nullptr;
  OfxPropertySetHandle sourceImg = nullptr;
  try {
    OfxStatus const outGot = gEffectHost->clipGetImage(outputClip, time, nullptr, &outputImg);
    if (outGot != kOfxStatOK) {
      report(instance, "clipGetImage Output fehlgeschlagen.");
      throw NoImageEx();
    }
    int dstRowBytes = 0;
    OfxRectI dstRect{};
    void *dstPtr = nullptr;
    gPropHost->propGetInt(outputImg, kOfxImagePropRowBytes, 0, &dstRowBytes);
    gPropHost->propGetIntN(outputImg, kOfxImagePropBounds, 4, &dstRect.x1);
    gPropHost->propGetPointer(outputImg, kOfxImagePropData, 0, &dstPtr);

    OfxStatus const srcGot = gEffectHost->clipGetImage(sourceClip, time, nullptr, &sourceImg);
    if (srcGot != kOfxStatOK) {
      report(instance, "clipGetImage Source fehlgeschlagen.");
      throw NoImageEx();
    }
    int srcRowBytes = 0;
    OfxRectI srcRect{};
    void *srcPtr = nullptr;
    gPropHost->propGetInt(sourceImg, kOfxImagePropRowBytes, 0, &srcRowBytes);
    gPropHost->propGetIntN(sourceImg, kOfxImagePropBounds, 4, &srcRect.x1);
    gPropHost->propGetPointer(sourceImg, kOfxImagePropData, 0, &srcPtr);
    if (!srcPtr || !dstPtr) {
      report(instance, "Bildpuffer ist leer.");
      throw NoImageEx();
    }

    int cudaEnabled = 0;
    gPropHost->propGetInt(inArgs, kOfxImageEffectPropCudaEnabled, 0, &cudaEnabled);
    if (cudaEnabled) {
      report(instance,
             "Host liefert CUDA-Gerätepuffer. Dieses Plugin rendert über CPU-Puffer (Kernels intern auf GPU).");
      throw NoImageEx();
    }

    char *srcDepthName = nullptr;
    char *dstDepthName = nullptr;
    gPropHost->propGetString(sourceImg, kOfxImageEffectPropPixelDepth, 0, &srcDepthName);
    gPropHost->propGetString(outputImg, kOfxImageEffectPropPixelDepth, 0, &dstDepthName);
    PixelDepth const srcDepth = parseDepth(srcDepthName);
    PixelDepth const dstDepth = parseDepth(dstDepthName);

    int const width = renderWindow.x2 - renderWindow.x1;
    int const height = renderWindow.y2 - renderWindow.y1;
    if (width <= 0 || height <= 0) {
      report(instance, "RenderWindow ist leer.");
      throw NoImageEx();
    }
    size_t const bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    std::vector<unsigned char> bufA(bytes);
    std::vector<unsigned char> bufB(bytes);
    packRgba(srcPtr, srcDepth, srcRect, srcRowBytes, renderWindow, bufA.data());
    unsigned char *in = bufA.data();
    unsigned char *out [[maybe_unused]] = bufB.data();
    std::string gpuError;
/*NR_STEPS*/
    if (status == kOfxStatOK) {
      unpackRgba(in, dstPtr, dstDepth, dstRect, dstRowBytes, renderWindow);
    } else if (!gpuError.empty()) {
      report(instance, gpuError.c_str());
    }
  } catch (NoImageEx &) {
    if (!gEffectHost->abort(instance)) {
      status = kOfxStatFailed;
    }
  }

  if (sourceImg) {
    gEffectHost->clipReleaseImage(sourceImg);
  }
  if (outputImg) {
    gEffectHost->clipReleaseImage(outputImg);
  }
  return status;
}

static OfxStatus describeInContext(OfxImageEffectHandle effect, OfxPropertySetHandle)
{
  OfxPropertySetHandle props = nullptr;
  gEffectHost->clipDefine(effect, kOfxImageEffectOutputClipName, &props);
  gPropHost->propSetString(props, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);
  gEffectHost->clipDefine(effect, kOfxImageEffectSimpleSourceClipName, &props);
  gPropHost->propSetString(props, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);
  return kOfxStatOK;
}

static OfxStatus describe(OfxImageEffectHandle effect)
{
  OfxPropertySetHandle effectProps = nullptr;
  gEffectHost->getPropertySet(effect, &effectProps);
  gPropHost->propSetInt(effectProps, kOfxImageEffectPropSupportsMultipleClipDepths, 0, 0);
  gPropHost->propSetInt(effectProps, kOfxImageEffectPropSupportsTiles, 0, 0);
  gPropHost->propSetInt(effectProps, kOfxImageEffectPropSupportsMultiResolution, 0, 0);
  gPropHost->propSetInt(effectProps, kOfxImageEffectPropTemporalClipAccess, 0, 0);
  gPropHost->propSetString(effectProps, kOfxImageEffectPluginRenderThreadSafety, 0, kOfxImageEffectRenderUnsafe);
  gPropHost->propSetString(effectProps, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthByte);
  gPropHost->propSetString(effectProps, kOfxImageEffectPropSupportedPixelDepths, 1, kOfxBitDepthShort);
  gPropHost->propSetString(effectProps, kOfxImageEffectPropSupportedPixelDepths, 2, kOfxBitDepthFloat);
  gPropHost->propSetString(effectProps, kOfxImageEffectPropCPURenderSupported, 0, "true");
  // false: Resolve soll CPU-Puffer (Byte/Short/Float) liefern. CUDA/OpenCL-Kernel laufen intern via NVRTC/OpenCL.dll.
  gPropHost->propSetString(effectProps, kOfxImageEffectPropCudaRenderSupported, 0, "false");
  gPropHost->propSetString(effectProps, kOfxImageEffectPropOpenCLRenderSupported, 0, "false");
  gPropHost->propSetString(effectProps, kOfxImageEffectPropOpenCLSupported, 0, "false");
  gPropHost->propSetString(effectProps, kOfxPropLabel, 0, "/*NR_LABEL*/");
  gPropHost->propSetString(effectProps, kOfxImageEffectPluginPropGrouping, 0, "NR OFX Host");
  gPropHost->propSetString(effectProps, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextFilter);
  return kOfxStatOK;
}

static OfxStatus onLoad(void)
{
  if (!gHost) {
    return kOfxStatErrMissingHostFeature;
  }
  gEffectHost = (OfxImageEffectSuiteV1 *)gHost->fetchSuite(gHost->host, kOfxImageEffectSuite, 1);
  gPropHost = (OfxPropertySuiteV1 *)gHost->fetchSuite(gHost->host, kOfxPropertySuite, 1);
  gMessageHost = (OfxMessageSuiteV1 *)gHost->fetchSuite(gHost->host, kOfxMessageSuite, 1);
  if (!gEffectHost || !gPropHost) {
    return kOfxStatErrMissingHostFeature;
  }
  return kOfxStatOK;
}

static OfxStatus pluginMain(char const *action, void const *handle, OfxPropertySetHandle inArgs,
                            OfxPropertySetHandle outArgs)
{
  try {
    auto effect = static_cast<OfxImageEffectHandle>(const_cast<void *>(handle));
    if (std::strcmp(action, kOfxActionLoad) == 0) {
      return onLoad();
    }
    if (std::strcmp(action, kOfxActionDescribe) == 0) {
      return describe(effect);
    }
    if (std::strcmp(action, kOfxImageEffectActionDescribeInContext) == 0) {
      return describeInContext(effect, inArgs);
    }
    if (std::strcmp(action, kOfxActionCreateInstance) == 0
        || std::strcmp(action, kOfxActionDestroyInstance) == 0
        || std::strcmp(action, kOfxActionUnload) == 0) {
      return kOfxStatOK;
    }
    if (std::strcmp(action, kOfxImageEffectActionRender) == 0) {
      return render(effect, inArgs, outArgs);
    }
  } catch (std::bad_alloc const &) {
    return kOfxStatErrMemory;
  } catch (std::exception const &) {
    return kOfxStatErrUnknown;
  } catch (...) {
    return kOfxStatErrUnknown;
  }
  return kOfxStatReplyDefault;
}

static void setHostFunc(OfxHost *hostStruct)
{
  gHost = hostStruct;
}

static OfxPlugin basicPlugin = {kOfxImageEffectPluginApi, 1, "/*NR_PLUGIN_ID*/", 1, 0, setHostFunc, pluginMain};

EXPORT OfxPlugin *OfxGetPlugin(int nth)
{
  return nth == 0 ? &basicPlugin : nullptr;
}

EXPORT int OfxGetNumberOfPlugins(void)
{
  return 1;
}
)CPP");
  text.replace(QStringLiteral("/*NR_CPU_INCLUDES*/"), includes);
  text.replace(QStringLiteral("/*NR_GPU_SOURCES*/"), gpuSources);
  text.replace(QStringLiteral("/*NR_STEPS*/"), steps);
  text.replace(QStringLiteral("/*NR_LABEL*/"), label);
  text.replace(QStringLiteral("/*NR_PLUGIN_ID*/"), pluginId);
  return text;
}

} // namespace

OfxExportResult OfxExporter::exportPipeline(Pipeline const &pipeline, QString const &projectDir, QString const &pluginName)
{
  OfxExportResult result;
  QString const name = sanitizeIdent(pluginName);
  QDir root(projectDir);
  if (!root.mkpath(QStringLiteral("."))) {
    result.error = QStringLiteral("Exportordner konnte nicht angelegt werden.");
    return result;
  }
  if (!root.mkpath(QStringLiteral("kernels"))) {
    result.error = QStringLiteral("kernels/ konnte nicht angelegt werden.");
    return result;
  }

  QHash<quint64, PipelineNode const *> byId;
  for (PipelineNode const &node : pipeline.nodes) {
    byId.insert(node.id, &node);
  }

  QVector<ExportStep> steps;
  int cpuIndex = 0;
  int gpuIndex = 0;
  QString cpuIncludes;
  QString gpuSources;

  for (PipelineNode const &node : pipeline.nodes) {
    if (node.kind == NodeKind::Plugin) {
      result.warnings.push_back(
          QStringLiteral("Plugin-Knoten %1 wird übersprungen (bereits ein .ofx).").arg(node.id));
    }
  }

  for (quint64 id : topoKernelIds(pipeline)) {
    PipelineNode const *node = byId.value(id);
    if (!node) {
      continue;
    }
    QString const source = node->params.value(QStringLiteral("source")).toString();
    if (node->kind == NodeKind::Python) {
      result.warnings.push_back(
          QStringLiteral("Python-Knoten %1 wird übersprungen (kein OFX).").arg(node->id));
      continue;
    }
    if (source.trimmed().isEmpty()) {
      result.warnings.push_back(QStringLiteral("Leerer Kernel an Knoten %1, übersprungen.").arg(node->id));
      continue;
    }
    ExportStep step;
    step.kind = node->kind;
    step.source = source;
    if (node->kind == NodeKind::Cpu) {
      step.index = cpuIndex;
      step.symbol = QStringLiteral("nr_process_%1").arg(cpuIndex);
      QString const rel = QStringLiteral("kernels/cpu_%1.cpp").arg(cpuIndex);
      if (!writeText(root.filePath(rel), source, &result.error)) {
        return result;
      }
      cpuIncludes += QStringLiteral("#define nr_process %1\n#include \"%2\"\n#undef nr_process\n")
                         .arg(step.symbol, rel);
      ++cpuIndex;
    } else {
      step.index = gpuIndex;
      QString const var = QStringLiteral("kGpuSource_%1").arg(gpuIndex);
      step.symbol = var;
      QString const ext = node->kind == NodeKind::Cuda ? QStringLiteral("cu") : QStringLiteral("cl");
      QString const rel = QStringLiteral("kernels/gpu_%1.%2").arg(gpuIndex).arg(ext);
      if (!writeText(root.filePath(rel), source, &result.error)) {
        return result;
      }
      gpuSources += QStringLiteral("static char const *%1 = %2;\n").arg(var, cppRaw(source));
      ++gpuIndex;
    }
    steps.push_back(step);
  }

  QString stepCode;
  for (ExportStep const &step : steps) {
    if (step.kind == NodeKind::Cpu) {
      stepCode += QStringLiteral(
          "    if (status == kOfxStatOK) {\n"
          "      NrCpuImage inImg{width, height, width * 4, in};\n"
          "      NrCpuImage outImg{width, height, width * 4, out};\n"
          "      if (%1(&inImg, &outImg) != 0) {\n"
          "        report(instance, \"CPU-Kernel fehlgeschlagen.\");\n"
          "        status = kOfxStatFailed;\n"
          "      } else {\n"
          "        std::swap(in, out);\n"
          "      }\n"
          "    }\n")
                      .arg(step.symbol);
    } else if (step.kind == NodeKind::Cuda) {
      stepCode += QStringLiteral(
          "    if (status == kOfxStatOK) {\n"
          "      if (!nrGpuLaunchCuda(%1, in, out, width, height, &gpuError)) {\n"
          "        report(instance, gpuError.empty() ? \"CUDA-Launch fehlgeschlagen.\" : gpuError.c_str());\n"
          "        status = kOfxStatFailed;\n"
          "      } else {\n"
          "        std::swap(in, out);\n"
          "      }\n"
          "    }\n")
                      .arg(step.symbol);
    } else if (step.kind == NodeKind::OpenCl) {
      stepCode += QStringLiteral(
          "    if (status == kOfxStatOK) {\n"
          "      if (!nrGpuLaunchOpenCl(%1, in, out, width, height, &gpuError)) {\n"
          "        report(instance, gpuError.empty() ? \"OpenCL-Launch fehlgeschlagen.\" : gpuError.c_str());\n"
          "        status = kOfxStatFailed;\n"
          "      } else {\n"
          "        std::swap(in, out);\n"
          "      }\n"
          "    }\n")
                      .arg(step.symbol);
    }
  }
  if (steps.isEmpty()) {
    result.warnings.push_back(QStringLiteral("Keine CPU/CUDA/OpenCL-Kernel — Plugin kopiert Source nach Output."));
  }

  QString const abi = QStringLiteral(R"(#pragma once
#ifdef __cplusplus
extern "C" {
#endif

typedef struct NrCpuImage {
  int width;
  int height;
  int stride;
  unsigned char *rgba;
} NrCpuImage;

int nr_process(NrCpuImage const *input, NrCpuImage *output);

#ifdef __cplusplus
}
#endif
)");

  QString const codegenDir = QString::fromUtf8(NR_CODEGEN_DIR);
  if (codegenDir.isEmpty() || !QFileInfo::exists(QDir(codegenDir).filePath(QStringLiteral("gpu_launch.cpp")))) {
    result.error = QStringLiteral("gpu_launch.cpp nicht gefunden (NR_CODEGEN_DIR).");
    return result;
  }
  if (!copyFile(QDir(codegenDir).filePath(QStringLiteral("gpu_launch.cpp")), root.filePath(QStringLiteral("gpu_launch.cpp")),
                &result.error)
      || !copyFile(QDir(codegenDir).filePath(QStringLiteral("gpu_launch.h")), root.filePath(QStringLiteral("gpu_launch.h")),
                   &result.error)) {
    return result;
  }

  QString const ofxInclude = QString::fromUtf8(NR_OPENFX_INCLUDE);
  if (ofxInclude.isEmpty() || !QFileInfo::exists(QDir(ofxInclude).filePath(QStringLiteral("ofxImageEffect.h")))) {
    result.error = QStringLiteral("OpenFX-Headers nicht gefunden (NR_OPENFX_INCLUDE).");
    return result;
  }

  if (!writeText(root.filePath(QStringLiteral("nr_cpu_abi.h")), abi, &result.error)
      || !writeText(root.filePath(QStringLiteral("plugin.cpp")),
                    pluginCpp(pluginIdFromName(name), name, cpuIncludes, gpuSources, stepCode), &result.error)
      || !writeText(root.filePath(QStringLiteral("CMakeLists.txt")), cmakeLists(name, ofxInclude), &result.error)
      || !writeText(root.filePath(QStringLiteral("README.md")), readme(name), &result.error)) {
    return result;
  }

  QString const bundleRel = QStringLiteral("%1.ofx.bundle").arg(name);
  QString const win64 = bundleRel + QStringLiteral("/Contents/Win64");
  if (!root.mkpath(win64)) {
    result.error = QStringLiteral("Bundle-Ordner konnte nicht angelegt werden.");
    return result;
  }
  if (!writeText(root.filePath(bundleRel + QStringLiteral("/Contents/Info.plist")),
                 infoPlist(name + QStringLiteral(".ofx")), &result.error)) {
    return result;
  }

  result.projectDir = QDir::cleanPath(root.absolutePath());
  result.bundlePath = root.filePath(bundleRel);

  QString const gxx = findGxx();
  QString const ofxOut = root.filePath(win64 + QLatin1Char('/') + name + QStringLiteral(".ofx"));
  QProcess compiler;
  compiler.setWorkingDirectory(root.absolutePath());
  compiler.setProcessChannelMode(QProcess::MergedChannels);
  QStringList args;
  args << QStringLiteral("-shared") << QStringLiteral("-std=c++20") << QStringLiteral("-O2")
       << QStringLiteral("-static-libgcc") << QStringLiteral("-static-libstdc++") << QStringLiteral("-I")
       << ofxInclude << QStringLiteral("-I") << root.absolutePath() << QStringLiteral("plugin.cpp")
       << QStringLiteral("gpu_launch.cpp") << QStringLiteral("-o") << ofxOut;
  compiler.start(gxx, args);
  if (!compiler.waitForFinished(120000) || compiler.exitCode() != 0) {
    result.warnings.push_back(
        QStringLiteral("MinGW-Build fehlgeschlagen (%1). CMake-Projekt liegt in %2.\n%3")
            .arg(gxx, result.projectDir, QString::fromUtf8(compiler.readAllStandardOutput())));
  } else {
    result.pluginPath = ofxOut;
  }

  result.ok = true;
  return result;
}
