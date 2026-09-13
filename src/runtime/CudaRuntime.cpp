#include "KernelRuntime.h"
#include "PortInference.h"

#include <QDir>
#include <QFileInfo>
#include <QLibrary>

#include <algorithm>

namespace {

using nvrtcResult = int;
using CUresult = int;
using CUdevice = int;
using CUcontext = void *;
using CUmodule = void *;
using CUfunction = void *;
using CUdeviceptr = unsigned long long;
using nvrtcProgram = void *;

constexpr int kNvrtcSuccess = 0;
constexpr int kCudaSuccess = 0;

struct CudaApi
{
  QLibrary driver;
  QLibrary nvrtc;
  CUresult (*cuInit)(unsigned int) = nullptr;
  CUresult (*cuDeviceGet)(CUdevice *, int) = nullptr;
  CUresult (*cuCtxCreate)(CUcontext *, unsigned int, CUdevice) = nullptr;
  CUresult (*cuCtxDestroy)(CUcontext) = nullptr;
  CUresult (*cuMemAlloc)(CUdeviceptr *, size_t) = nullptr;
  CUresult (*cuMemFree)(CUdeviceptr) = nullptr;
  CUresult (*cuMemcpyHtoD)(CUdeviceptr, void const *, size_t) = nullptr;
  CUresult (*cuMemcpyDtoH)(void *, CUdeviceptr, size_t) = nullptr;
  CUresult (*cuModuleLoadData)(CUmodule *, void const *) = nullptr;
  CUresult (*cuModuleUnload)(CUmodule) = nullptr;
  CUresult (*cuModuleGetFunction)(CUfunction *, CUmodule, char const *) = nullptr;
  CUresult (*cuLaunchKernel)(CUfunction, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int,
                             unsigned int, unsigned int, void *, void **, void **) = nullptr;
  CUresult (*cuCtxSynchronize)() = nullptr;
  nvrtcResult (*nvrtcCreateProgram)(nvrtcProgram *, char const *, char const *, int, char const *const *,
                                    char const *const *) = nullptr;
  nvrtcResult (*nvrtcDestroyProgram)(nvrtcProgram *) = nullptr;
  nvrtcResult (*nvrtcCompileProgram)(nvrtcProgram, int, char const *const *) = nullptr;
  nvrtcResult (*nvrtcGetPTXSize)(nvrtcProgram, size_t *) = nullptr;
  nvrtcResult (*nvrtcGetPTX)(nvrtcProgram, char *) = nullptr;
  nvrtcResult (*nvrtcGetProgramLogSize)(nvrtcProgram, size_t *) = nullptr;
  nvrtcResult (*nvrtcGetProgramLog)(nvrtcProgram, char *) = nullptr;
};

QString findNvrtc()
{
  QString const cudaPath = qEnvironmentVariable("CUDA_PATH");
  QStringList dirs;
  if (!cudaPath.isEmpty()) {
    dirs << QDir(cudaPath).filePath(QStringLiteral("bin"));
  }
  dirs << QStringLiteral("C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6/bin")
       << QStringLiteral("C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.5/bin")
       << QStringLiteral("C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.4/bin")
       << QStringLiteral("C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.3/bin")
       << QStringLiteral("C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.2/bin")
       << QStringLiteral("C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.1/bin")
       << QStringLiteral("C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.0/bin")
       << QStringLiteral("C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.8/bin");
  QStringList names = {QStringLiteral("nvrtc64_120_0.dll"),
                       QStringLiteral("nvrtc64_112_0.dll"),
                       QStringLiteral("nvrtc64_111_0.dll"),
                       QStringLiteral("nvrtc64_110_0.dll")};
  for (QString const &dir : dirs) {
    for (QString const &name : names) {
      QString const path = QDir(dir).filePath(name);
      if (QFileInfo::exists(path)) {
        return path;
      }
    }
    QStringList const found = QDir(dir).entryList({QStringLiteral("nvrtc64_*.dll")}, QDir::Files);
    if (!found.isEmpty()) {
      return QDir(dir).filePath(found.first());
    }
  }
  return QStringLiteral("nvrtc64_120_0");
}

CudaApi *api()
{
  static CudaApi instance;
  static bool attempted = false;
  if (!attempted) {
    attempted = true;
    instance.driver.setFileName(QStringLiteral("nvcuda"));
    instance.nvrtc.setFileName(findNvrtc());
    if (!instance.driver.load() || !instance.nvrtc.load()) {
      return &instance;
    }
#define NR_CU(name, sym) instance.name = reinterpret_cast<decltype(instance.name)>(instance.driver.resolve(sym))
    NR_CU(cuInit, "cuInit");
    NR_CU(cuDeviceGet, "cuDeviceGet");
    NR_CU(cuCtxCreate, "cuCtxCreate_v2");
    NR_CU(cuCtxDestroy, "cuCtxDestroy_v2");
    NR_CU(cuMemAlloc, "cuMemAlloc_v2");
    NR_CU(cuMemFree, "cuMemFree_v2");
    NR_CU(cuMemcpyHtoD, "cuMemcpyHtoD_v2");
    NR_CU(cuMemcpyDtoH, "cuMemcpyDtoH_v2");
    NR_CU(cuModuleLoadData, "cuModuleLoadData");
    NR_CU(cuModuleUnload, "cuModuleUnload");
    NR_CU(cuModuleGetFunction, "cuModuleGetFunction");
    NR_CU(cuLaunchKernel, "cuLaunchKernel");
    NR_CU(cuCtxSynchronize, "cuCtxSynchronize");
#undef NR_CU
#define NR_RTC(name) instance.name = reinterpret_cast<decltype(instance.name)>(instance.nvrtc.resolve(#name))
    NR_RTC(nvrtcCreateProgram);
    NR_RTC(nvrtcDestroyProgram);
    NR_RTC(nvrtcCompileProgram);
    NR_RTC(nvrtcGetPTXSize);
    NR_RTC(nvrtcGetPTX);
    NR_RTC(nvrtcGetProgramLogSize);
    NR_RTC(nvrtcGetProgramLog);
#undef NR_RTC
  }
  return &instance;
}

bool apiReady()
{
  CudaApi *a = api();
  return a->driver.isLoaded() && a->nvrtc.isLoaded() && a->cuInit && a->nvrtcCompileProgram && a->cuLaunchKernel;
}

class CudaRuntime final : public KernelRuntime
{
public:
  ~CudaRuntime() override { reset(); }

  bool load(QString const &source, QString *error) override
  {
    reset();
    if (!apiReady()) {
      if (error) {
        *error = QStringLiteral("CUDA/NVRTC nicht gefunden. CUDA Toolkit installieren oder CUDA_PATH setzen.");
      }
      return false;
    }
    CudaApi *a = api();
    if (a->cuInit(0) != kCudaSuccess) {
      if (error) {
        *error = QStringLiteral("cuInit fehlgeschlagen.");
      }
      return false;
    }
    CUdevice device = 0;
    if (a->cuDeviceGet(&device, 0) != kCudaSuccess) {
      if (error) {
        *error = QStringLiteral("Kein CUDA-Gerät.");
      }
      return false;
    }
    if (a->cuCtxCreate(&m_context, 0, device) != kCudaSuccess) {
      if (error) {
        *error = QStringLiteral("CUDA-Kontext fehlgeschlagen.");
      }
      return false;
    }

    QByteArray src = source.toUtf8();
    nvrtcProgram program = nullptr;
    if (a->nvrtcCreateProgram(&program, src.constData(), "kernel.cu", 0, nullptr, nullptr) != kNvrtcSuccess) {
      if (error) {
        *error = QStringLiteral("nvrtcCreateProgram fehlgeschlagen.");
      }
      return false;
    }
    nvrtcResult const rc = a->nvrtcCompileProgram(program, 0, nullptr);
    if (rc != kNvrtcSuccess) {
      size_t logSize = 0;
      a->nvrtcGetProgramLogSize(program, &logSize);
      QByteArray log(int(logSize), 0);
      a->nvrtcGetProgramLog(program, log.data());
      a->nvrtcDestroyProgram(&program);
      if (error) {
        *error = QString::fromUtf8(log).trimmed();
        if (error->isEmpty()) {
          *error = QStringLiteral("NVRTC-Kompilierung fehlgeschlagen.");
        }
      }
      return false;
    }
    size_t ptxSize = 0;
    a->nvrtcGetPTXSize(program, &ptxSize);
    QByteArray ptx(int(ptxSize), 0);
    a->nvrtcGetPTX(program, ptx.data());
    a->nvrtcDestroyProgram(&program);

    if (a->cuModuleLoadData(&m_module, ptx.constData()) != kCudaSuccess) {
      if (error) {
        *error = QStringLiteral("cuModuleLoadData fehlgeschlagen.");
      }
      return false;
    }
    if (a->cuModuleGetFunction(&m_function, m_module, "process") != kCudaSuccess) {
      if (error) {
        *error = QStringLiteral("CUDA-Kernel 'process' fehlt.");
      }
      return false;
    }
    m_layout = inferKernelPorts(NodeKind::Cuda, source);
    return true;
  }

  bool process(QVector<RgbaImage> const &inputs, QVector<RgbaImage> *outputs, QString *error) override
  {
    if (!m_function || !outputs || inputs.isEmpty()) {
      if (error) {
        *error = QStringLiteral("CUDA-Kernel ist nicht geladen.");
      }
      return false;
    }
    CudaApi *a = api();
    int const nIn = std::min(m_layout.inCount, int(inputs.size()));
    int const nOut = m_layout.outCount;
    size_t const bytes = size_t(inputs[0].byteCount());
    QVector<CUdeviceptr> inDev(nIn, 0);
    QVector<CUdeviceptr> outDev(nOut, 0);
    auto cleanup = [&]() {
      for (CUdeviceptr p : inDev) {
        if (p) {
          a->cuMemFree(p);
        }
      }
      for (CUdeviceptr p : outDev) {
        if (p) {
          a->cuMemFree(p);
        }
      }
    };
    for (int i = 0; i < nIn; ++i) {
      if (a->cuMemAlloc(&inDev[i], bytes) != kCudaSuccess) {
        cleanup();
        if (error) {
          *error = QStringLiteral("CUDA-Malloc fehlgeschlagen.");
        }
        return false;
      }
      a->cuMemcpyHtoD(inDev[i], inputs[i].pixels.constData(), bytes);
    }
    for (int i = 0; i < nOut; ++i) {
      if (a->cuMemAlloc(&outDev[i], bytes) != kCudaSuccess) {
        cleanup();
        if (error) {
          *error = QStringLiteral("CUDA-Malloc fehlgeschlagen.");
        }
        return false;
      }
    }
    QVector<void *> args;
    for (int i = 0; i < nIn; ++i) {
      args.push_back(&inDev[i]);
    }
    for (int i = 0; i < nOut; ++i) {
      args.push_back(&outDev[i]);
    }
    int pixels = inputs[0].pixelCount();
    args.push_back(&pixels);
    unsigned int const block = 256;
    unsigned int const grid = unsigned((pixels + int(block) - 1) / int(block));
    CUresult const launch =
      a->cuLaunchKernel(m_function, grid, 1, 1, block, 1, 1, 0, nullptr, args.data(), nullptr);
    if (launch != kCudaSuccess) {
      cleanup();
      if (error) {
        *error = QStringLiteral("CUDA-Launch fehlgeschlagen (%1).").arg(launch);
      }
      return false;
    }
    a->cuCtxSynchronize();
    outputs->resize(nOut);
    for (int i = 0; i < nOut; ++i) {
      (*outputs)[i].width = inputs[0].width;
      (*outputs)[i].height = inputs[0].height;
      (*outputs)[i].pixels.resize(int(bytes));
      a->cuMemcpyDtoH((*outputs)[i].pixels.data(), outDev[i], bytes);
    }
    cleanup();
    return true;
  }

private:
  void reset()
  {
    CudaApi *a = api();
    if (a->cuModuleUnload && m_module) {
      a->cuModuleUnload(m_module);
    }
    if (a->cuCtxDestroy && m_context) {
      a->cuCtxDestroy(m_context);
    }
    m_module = nullptr;
    m_function = nullptr;
    m_context = nullptr;
  }

  CUcontext m_context = nullptr;
  CUmodule m_module = nullptr;
  CUfunction m_function = nullptr;
  KernelPortLayout m_layout;
};

} // namespace

std::unique_ptr<KernelRuntime> createCudaRuntime()
{
  return std::make_unique<CudaRuntime>();
}
