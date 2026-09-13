#include "KernelRuntime.h"
#include "PortInference.h"

#include <QLibrary>
#include <QVector>

#include <algorithm>

namespace {

using cl_int = int;
using cl_uint = unsigned int;
using cl_ulong = unsigned long long;
using cl_bitfield = cl_ulong;
using cl_device_type = cl_bitfield;
using cl_mem_flags = cl_bitfield;
using cl_command_queue_properties = cl_bitfield;
using cl_platform_id = struct _cl_platform_id *;
using cl_device_id = struct _cl_device_id *;
using cl_context = struct _cl_context *;
using cl_command_queue = struct _cl_command_queue *;
using cl_mem = struct _cl_mem *;
using cl_program = struct _cl_program *;
using cl_kernel = struct _cl_kernel *;

constexpr cl_int kClSuccess = 0;
constexpr cl_device_type kClDeviceTypeGpu = 1ull << 2;
constexpr cl_device_type kClDeviceTypeCpu = 1ull << 1;
constexpr cl_mem_flags kClMemReadOnly = 1ull << 2;
constexpr cl_mem_flags kClMemWriteOnly = 1ull << 1;
constexpr cl_mem_flags kClMemCopyHostPtr = 1ull << 5;
constexpr cl_uint kClProgramBuildLog = 0x1183;

struct OpenClApi
{
  QLibrary lib;
  cl_int (*clGetPlatformIDs)(cl_uint, cl_platform_id *, cl_uint *) = nullptr;
  cl_int (*clGetDeviceIDs)(cl_platform_id, cl_device_type, cl_uint, cl_device_id *, cl_uint *) = nullptr;
  cl_context (*clCreateContext)(void const *, cl_uint, cl_device_id const *, void *, void *, cl_int *) = nullptr;
  cl_command_queue (*clCreateCommandQueue)(cl_context, cl_device_id, cl_command_queue_properties, cl_int *) = nullptr;
  cl_mem (*clCreateBuffer)(cl_context, cl_mem_flags, size_t, void *, cl_int *) = nullptr;
  cl_program (*clCreateProgramWithSource)(cl_context, cl_uint, char const **, size_t const *, cl_int *) = nullptr;
  cl_int (*clBuildProgram)(cl_program, cl_uint, cl_device_id const *, char const *, void *, void *) = nullptr;
  cl_int (*clGetProgramBuildInfo)(cl_program, cl_device_id, cl_uint, size_t, void *, size_t *) = nullptr;
  cl_kernel (*clCreateKernel)(cl_program, char const *, cl_int *) = nullptr;
  cl_int (*clSetKernelArg)(cl_kernel, cl_uint, size_t, void const *) = nullptr;
  cl_int (*clEnqueueNDRangeKernel)(cl_command_queue, cl_kernel, cl_uint, size_t const *, size_t const *,
                                   size_t const *, cl_uint, void const *, void *) = nullptr;
  cl_int (*clEnqueueReadBuffer)(cl_command_queue, cl_mem, cl_uint, size_t, size_t, void *, cl_uint, void const *,
                                void *) = nullptr;
  cl_int (*clFinish)(cl_command_queue) = nullptr;
  cl_int (*clReleaseMemObject)(cl_mem) = nullptr;
  cl_int (*clReleaseKernel)(cl_kernel) = nullptr;
  cl_int (*clReleaseProgram)(cl_program) = nullptr;
  cl_int (*clReleaseCommandQueue)(cl_command_queue) = nullptr;
  cl_int (*clReleaseContext)(cl_context) = nullptr;
};

OpenClApi *api()
{
  static OpenClApi instance;
  static bool attempted = false;
  if (!attempted) {
    attempted = true;
    instance.lib.setFileName(QStringLiteral("OpenCL"));
    if (!instance.lib.load()) {
      return &instance;
    }
#define NR_CL_LOAD(name) instance.name = reinterpret_cast<decltype(instance.name)>(instance.lib.resolve(#name))
    NR_CL_LOAD(clGetPlatformIDs);
    NR_CL_LOAD(clGetDeviceIDs);
    NR_CL_LOAD(clCreateContext);
    NR_CL_LOAD(clCreateCommandQueue);
    NR_CL_LOAD(clCreateBuffer);
    NR_CL_LOAD(clCreateProgramWithSource);
    NR_CL_LOAD(clBuildProgram);
    NR_CL_LOAD(clGetProgramBuildInfo);
    NR_CL_LOAD(clCreateKernel);
    NR_CL_LOAD(clSetKernelArg);
    NR_CL_LOAD(clEnqueueNDRangeKernel);
    NR_CL_LOAD(clEnqueueReadBuffer);
    NR_CL_LOAD(clFinish);
    NR_CL_LOAD(clReleaseMemObject);
    NR_CL_LOAD(clReleaseKernel);
    NR_CL_LOAD(clReleaseProgram);
    NR_CL_LOAD(clReleaseCommandQueue);
    NR_CL_LOAD(clReleaseContext);
#undef NR_CL_LOAD
  }
  return &instance;
}

bool apiReady()
{
  OpenClApi *a = api();
  return a->lib.isLoaded() && a->clGetPlatformIDs && a->clCreateKernel && a->clEnqueueNDRangeKernel;
}

class OpenClRuntime final : public KernelRuntime
{
public:
  ~OpenClRuntime() override { reset(); }

  bool load(QString const &source, QString *error) override
  {
    reset();
    if (!apiReady()) {
      if (error) {
        *error = QStringLiteral("OpenCL.dll nicht gefunden oder unvollständig.");
      }
      return false;
    }
    OpenClApi *a = api();
    cl_int err = 0;
    cl_uint platformCount = 0;
    if (a->clGetPlatformIDs(0, nullptr, &platformCount) != kClSuccess || platformCount == 0) {
      if (error) {
        *error = QStringLiteral("Keine OpenCL-Plattform.");
      }
      return false;
    }
    QVector<cl_platform_id> platforms;
    platforms.resize(int(platformCount));
    a->clGetPlatformIDs(platformCount, platforms.data(), nullptr);

    cl_device_id device = nullptr;
    for (cl_platform_id platform : platforms) {
      if (a->clGetDeviceIDs(platform, kClDeviceTypeGpu, 1, &device, nullptr) == kClSuccess) {
        break;
      }
      device = nullptr;
    }
    if (!device) {
      for (cl_platform_id platform : platforms) {
        if (a->clGetDeviceIDs(platform, kClDeviceTypeCpu, 1, &device, nullptr) == kClSuccess) {
          break;
        }
        device = nullptr;
      }
    }
    if (!device) {
      if (error) {
        *error = QStringLiteral("Kein OpenCL-Gerät.");
      }
      return false;
    }

    m_context = a->clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err);
    if (!m_context || err != kClSuccess) {
      if (error) {
        *error = QStringLiteral("OpenCL-Kontext fehlgeschlagen (%1).").arg(err);
      }
      return false;
    }
    m_queue = a->clCreateCommandQueue(m_context, device, 0, &err);
    if (!m_queue || err != kClSuccess) {
      if (error) {
        *error = QStringLiteral("OpenCL-Queue fehlgeschlagen (%1).").arg(err);
      }
      return false;
    }

    QByteArray src = source.toUtf8();
    char const *srcPtr = src.constData();
    size_t srcLen = size_t(src.size());
    m_program = a->clCreateProgramWithSource(m_context, 1, &srcPtr, &srcLen, &err);
    if (!m_program || err != kClSuccess) {
      if (error) {
        *error = QStringLiteral("OpenCL-Programm konnte nicht erzeugt werden.");
      }
      return false;
    }
    err = a->clBuildProgram(m_program, 1, &device, nullptr, nullptr, nullptr);
    if (err != kClSuccess) {
      size_t logSize = 0;
      a->clGetProgramBuildInfo(m_program, device, kClProgramBuildLog, 0, nullptr, &logSize);
      QByteArray log(int(logSize), 0);
      a->clGetProgramBuildInfo(m_program, device, kClProgramBuildLog, logSize, log.data(), nullptr);
      if (error) {
        *error = QString::fromUtf8(log).trimmed();
        if (error->isEmpty()) {
          *error = QStringLiteral("OpenCL-Build fehlgeschlagen (%1).").arg(err);
        }
      }
      return false;
    }
    m_kernel = a->clCreateKernel(m_program, "process", &err);
    if (!m_kernel || err != kClSuccess) {
      if (error) {
        *error = QStringLiteral("OpenCL-Kernel 'process' fehlt.");
      }
      return false;
    }
    m_device = device;
    m_layout = inferKernelPorts(NodeKind::OpenCl, source);
    return true;
  }

  bool process(QVector<RgbaImage> const &inputs, QVector<RgbaImage> *outputs, QString *error) override
  {
    if (!m_kernel || !outputs || inputs.isEmpty()) {
      if (error) {
        *error = QStringLiteral("OpenCL-Kernel ist nicht geladen.");
      }
      return false;
    }
    OpenClApi *a = api();
    cl_int err = 0;
    int const nIn = std::min(m_layout.inCount, int(inputs.size()));
    int const nOut = m_layout.outCount;
    size_t const bytes = size_t(inputs[0].byteCount());
    QVector<cl_mem> inBufs(nIn, nullptr);
    QVector<cl_mem> outBufs(nOut, nullptr);
    auto cleanup = [&]() {
      for (cl_mem b : inBufs) {
        if (b) {
          a->clReleaseMemObject(b);
        }
      }
      for (cl_mem b : outBufs) {
        if (b) {
          a->clReleaseMemObject(b);
        }
      }
    };
    for (int i = 0; i < nIn; ++i) {
      inBufs[i] = a->clCreateBuffer(m_context, kClMemReadOnly | kClMemCopyHostPtr, bytes,
                                    const_cast<char *>(inputs[i].pixels.constData()), &err);
      if (!inBufs[i] || err != kClSuccess) {
        cleanup();
        if (error) {
          *error = QStringLiteral("OpenCL-Input-Buffer fehlgeschlagen.");
        }
        return false;
      }
    }
    for (int i = 0; i < nOut; ++i) {
      outBufs[i] = a->clCreateBuffer(m_context, kClMemWriteOnly, bytes, nullptr, &err);
      if (!outBufs[i] || err != kClSuccess) {
        cleanup();
        if (error) {
          *error = QStringLiteral("OpenCL-Output-Buffer fehlgeschlagen.");
        }
        return false;
      }
    }
    int arg = 0;
    for (int i = 0; i < nIn; ++i) {
      a->clSetKernelArg(m_kernel, cl_uint(arg++), sizeof(cl_mem), &inBufs[i]);
    }
    for (int i = 0; i < nOut; ++i) {
      a->clSetKernelArg(m_kernel, cl_uint(arg++), sizeof(cl_mem), &outBufs[i]);
    }
    int pixels = inputs[0].pixelCount();
    a->clSetKernelArg(m_kernel, cl_uint(arg++), sizeof(int), &pixels);
    size_t global = size_t(pixels);
    err = a->clEnqueueNDRangeKernel(m_queue, m_kernel, 1, nullptr, &global, nullptr, 0, nullptr, nullptr);
    if (err != kClSuccess) {
      cleanup();
      if (error) {
        *error = QStringLiteral("OpenCL-Launch fehlgeschlagen (%1).").arg(err);
      }
      return false;
    }
    outputs->resize(nOut);
    for (int i = 0; i < nOut; ++i) {
      (*outputs)[i].width = inputs[0].width;
      (*outputs)[i].height = inputs[0].height;
      (*outputs)[i].pixels.resize(int(bytes));
      err = a->clEnqueueReadBuffer(m_queue, outBufs[i], 1, 0, bytes, (*outputs)[i].pixels.data(), 0, nullptr,
                                   nullptr);
      if (err != kClSuccess) {
        cleanup();
        if (error) {
          *error = QStringLiteral("OpenCL-Read fehlgeschlagen (%1).").arg(err);
        }
        return false;
      }
    }
    a->clFinish(m_queue);
    cleanup();
    return true;
  }

private:
  void reset()
  {
    OpenClApi *a = api();
    if (a->clReleaseKernel && m_kernel) {
      a->clReleaseKernel(m_kernel);
    }
    if (a->clReleaseProgram && m_program) {
      a->clReleaseProgram(m_program);
    }
    if (a->clReleaseCommandQueue && m_queue) {
      a->clReleaseCommandQueue(m_queue);
    }
    if (a->clReleaseContext && m_context) {
      a->clReleaseContext(m_context);
    }
    m_kernel = nullptr;
    m_program = nullptr;
    m_queue = nullptr;
    m_context = nullptr;
    m_device = nullptr;
  }

  cl_context m_context = nullptr;
  cl_command_queue m_queue = nullptr;
  cl_program m_program = nullptr;
  cl_kernel m_kernel = nullptr;
  cl_device_id m_device = nullptr;
  KernelPortLayout m_layout;
};

} // namespace

std::unique_ptr<KernelRuntime> createOpenClRuntime()
{
  return std::make_unique<OpenClRuntime>();
}
