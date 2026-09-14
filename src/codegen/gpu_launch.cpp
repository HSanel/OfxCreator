#include "gpu_launch.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

#include <cstring>
#include <vector>

namespace {

#ifdef _WIN32
HMODULE loadLib(char const *name)
{
  return LoadLibraryA(name);
}

template<typename T>
T loadSym(HMODULE m, char const *name)
{
  return m ? reinterpret_cast<T>(GetProcAddress(m, name)) : nullptr;
}
#endif

} // namespace

bool nrGpuLaunchOpenCl(char const *source, unsigned char const *input, unsigned char *output, int width, int height,
                       std::string *error)
{
#ifdef _WIN32
  using cl_int = int;
  using cl_uint = unsigned int;
  using cl_ulong = unsigned long long;
  using cl_platform_id = struct _cl_platform_id *;
  using cl_device_id = struct _cl_device_id *;
  using cl_context = struct _cl_context *;
  using cl_command_queue = struct _cl_command_queue *;
  using cl_mem = struct _cl_mem *;
  using cl_program = struct _cl_program *;
  using cl_kernel = struct _cl_kernel *;

  HMODULE lib = loadLib("OpenCL.dll");
  if (!lib) {
    if (error) {
      *error = "OpenCL.dll nicht gefunden.";
    }
    return false;
  }

  auto clGetPlatformIDs = loadSym<cl_int (*)(cl_uint, cl_platform_id *, cl_uint *)>(lib, "clGetPlatformIDs");
  auto clGetDeviceIDs =
      loadSym<cl_int (*)(cl_platform_id, cl_ulong, cl_uint, cl_device_id *, cl_uint *)>(lib, "clGetDeviceIDs");
  auto clCreateContext =
      loadSym<cl_context (*)(void const *, cl_uint, cl_device_id const *, void *, void *, cl_int *)>(lib,
                                                                                                    "clCreateContext");
  auto clCreateCommandQueue =
      loadSym<cl_command_queue (*)(cl_context, cl_device_id, cl_ulong, cl_int *)>(lib, "clCreateCommandQueue");
  auto clCreateBuffer = loadSym<cl_mem (*)(cl_context, cl_ulong, size_t, void *, cl_int *)>(lib, "clCreateBuffer");
  auto clCreateProgramWithSource =
      loadSym<cl_program (*)(cl_context, cl_uint, char const **, size_t const *, cl_int *)>(lib,
                                                                                           "clCreateProgramWithSource");
  auto clBuildProgram =
      loadSym<cl_int (*)(cl_program, cl_uint, cl_device_id const *, char const *, void *, void *)>(lib, "clBuildProgram");
  auto clGetProgramBuildInfo =
      loadSym<cl_int (*)(cl_program, cl_device_id, cl_uint, size_t, void *, size_t *)>(lib, "clGetProgramBuildInfo");
  auto clCreateKernel = loadSym<cl_kernel (*)(cl_program, char const *, cl_int *)>(lib, "clCreateKernel");
  auto clSetKernelArg = loadSym<cl_int (*)(cl_kernel, cl_uint, size_t, void const *)>(lib, "clSetKernelArg");
  auto clEnqueueNDRangeKernel = loadSym<cl_int (*)(cl_command_queue, cl_kernel, cl_uint, size_t const *, size_t const *,
                                                   size_t const *, cl_uint, void const *, void *)>(
      lib, "clEnqueueNDRangeKernel");
  auto clEnqueueReadBuffer = loadSym<cl_int (*)(cl_command_queue, cl_mem, cl_uint, size_t, size_t, void *, cl_uint,
                                                void const *, void *)>(lib, "clEnqueueReadBuffer");
  auto clFinish = loadSym<cl_int (*)(cl_command_queue)>(lib, "clFinish");
  auto clReleaseMemObject = loadSym<cl_int (*)(cl_mem)>(lib, "clReleaseMemObject");
  auto clReleaseKernel = loadSym<cl_int (*)(cl_kernel)>(lib, "clReleaseKernel");
  auto clReleaseProgram = loadSym<cl_int (*)(cl_program)>(lib, "clReleaseProgram");
  auto clReleaseCommandQueue = loadSym<cl_int (*)(cl_command_queue)>(lib, "clReleaseCommandQueue");
  auto clReleaseContext = loadSym<cl_int (*)(cl_context)>(lib, "clReleaseContext");

  if (!clGetPlatformIDs || !clCreateKernel) {
    if (error) {
      *error = "OpenCL-Symbole fehlen.";
    }
    return false;
  }

  int const pixels = width * height;
  size_t const bytes = static_cast<size_t>(pixels) * 4u;
  cl_int err = 0;
  cl_uint nPlat = 0;
  if (clGetPlatformIDs(0, nullptr, &nPlat) != 0 || nPlat == 0) {
    if (error) {
      *error = "Keine OpenCL-Plattform.";
    }
    return false;
  }
  std::vector<cl_platform_id> plats(nPlat);
  clGetPlatformIDs(nPlat, plats.data(), nullptr);
  cl_device_id device = nullptr;
  for (cl_platform_id plat : plats) {
    if (clGetDeviceIDs(plat, 1ull << 2, 1, &device, nullptr) == 0) {
      break;
    }
    if (clGetDeviceIDs(plat, 1ull << 1, 1, &device, nullptr) == 0) {
      break;
    }
  }
  if (!device) {
    if (error) {
      *error = "Kein OpenCL-Gerät.";
    }
    return false;
  }
  cl_context ctx = clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err);
  cl_command_queue q = clCreateCommandQueue(ctx, device, 0, &err);
  cl_mem inBuf = clCreateBuffer(ctx, (1ull << 2) | (1ull << 5), bytes, const_cast<unsigned char *>(input), &err);
  cl_mem outBuf = clCreateBuffer(ctx, 1ull << 1, bytes, nullptr, &err);
  char const *src = source;
  cl_program prog = clCreateProgramWithSource(ctx, 1, &src, nullptr, &err);
  if (clBuildProgram(prog, 1, &device, nullptr, nullptr, nullptr) != 0) {
    size_t logSize = 0;
    clGetProgramBuildInfo(prog, device, 0x1183, 0, nullptr, &logSize);
    std::string log(logSize, '\0');
    clGetProgramBuildInfo(prog, device, 0x1183, logSize, log.data(), nullptr);
    if (error) {
      *error = log;
    }
    clReleaseProgram(prog);
    clReleaseMemObject(inBuf);
    clReleaseMemObject(outBuf);
    clReleaseCommandQueue(q);
    clReleaseContext(ctx);
    return false;
  }
  cl_kernel k = clCreateKernel(prog, "process", &err);
  if (!k) {
    if (error) {
      *error = "OpenCL-Kernel 'process' fehlt.";
    }
    return false;
  }
  clSetKernelArg(k, 0, sizeof(cl_mem), &inBuf);
  clSetKernelArg(k, 1, sizeof(cl_mem), &outBuf);
  clSetKernelArg(k, 2, sizeof(int), &pixels);
  size_t g = static_cast<size_t>(pixels);
  clEnqueueNDRangeKernel(q, k, 1, nullptr, &g, nullptr, 0, nullptr, nullptr);
  clEnqueueReadBuffer(q, outBuf, 1, 0, bytes, output, 0, nullptr, nullptr);
  clFinish(q);
  clReleaseKernel(k);
  clReleaseProgram(prog);
  clReleaseMemObject(inBuf);
  clReleaseMemObject(outBuf);
  clReleaseCommandQueue(q);
  clReleaseContext(ctx);
  return true;
#else
  (void)source;
  (void)input;
  (void)output;
  (void)width;
  (void)height;
  if (error) {
    *error = "OpenCL-Launch nur unter Windows.";
  }
  return false;
#endif
}

bool nrGpuLaunchCuda(char const *source, unsigned char const *input, unsigned char *output, int width, int height,
                     std::string *error)
{
#ifdef _WIN32
  using CUresult = int;
  using CUdevice = int;
  using CUcontext = struct CUctx_st *;
  using CUmodule = struct CUmod_st *;
  using CUfunction = struct CUfunc_st *;
  using CUdeviceptr = unsigned long long;
  using nvrtcProgram = struct _nvrtcProgram *;
  using nvrtcResult = int;

  HMODULE driver = loadLib("nvcuda.dll");
  HMODULE nvrtc = loadLib("nvrtc64_120_0.dll");
  if (!nvrtc) {
    nvrtc = loadLib("nvrtc64_130_0.dll");
  }
  if (!nvrtc) {
    nvrtc = loadLib("nvrtc64_110_0.dll");
  }
  if (!driver || !nvrtc) {
    if (error) {
      *error = "CUDA/NVRTC nicht gefunden.";
    }
    return false;
  }

  auto cuInit = loadSym<CUresult (*)(unsigned int)>(driver, "cuInit");
  auto cuDeviceGet = loadSym<CUresult (*)(CUdevice *, int)>(driver, "cuDeviceGet");
  auto cuCtxCreate = loadSym<CUresult (*)(CUcontext *, unsigned int, CUdevice)>(driver, "cuCtxCreate_v2");
  auto cuCtxDestroy = loadSym<CUresult (*)(CUcontext)>(driver, "cuCtxDestroy_v2");
  auto cuMemAlloc = loadSym<CUresult (*)(CUdeviceptr *, size_t)>(driver, "cuMemAlloc_v2");
  auto cuMemFree = loadSym<CUresult (*)(CUdeviceptr)>(driver, "cuMemFree_v2");
  auto cuMemcpyHtoD = loadSym<CUresult (*)(CUdeviceptr, void const *, size_t)>(driver, "cuMemcpyHtoD_v2");
  auto cuMemcpyDtoH = loadSym<CUresult (*)(void *, CUdeviceptr, size_t)>(driver, "cuMemcpyDtoH_v2");
  auto cuModuleLoadData = loadSym<CUresult (*)(CUmodule *, void const *)>(driver, "cuModuleLoadData");
  auto cuModuleUnload = loadSym<CUresult (*)(CUmodule)>(driver, "cuModuleUnload");
  auto cuModuleGetFunction = loadSym<CUresult (*)(CUfunction *, CUmodule, char const *)>(driver, "cuModuleGetFunction");
  auto cuLaunchKernel = loadSym<CUresult (*)(CUfunction, unsigned int, unsigned int, unsigned int, unsigned int,
                                             unsigned int, unsigned int, unsigned int, void *, void **, void **)>(
      driver, "cuLaunchKernel");
  auto cuCtxSynchronize = loadSym<CUresult (*)()>(driver, "cuCtxSynchronize");
  auto nvrtcCreateProgram =
      loadSym<nvrtcResult (*)(nvrtcProgram *, char const *, char const *, int, char const *const *, char const *const *)>(
          nvrtc, "nvrtcCreateProgram");
  auto nvrtcDestroyProgram = loadSym<nvrtcResult (*)(nvrtcProgram *)>(nvrtc, "nvrtcDestroyProgram");
  auto nvrtcCompileProgram = loadSym<nvrtcResult (*)(nvrtcProgram, int, char const *const *)>(nvrtc, "nvrtcCompileProgram");
  auto nvrtcGetPTXSize = loadSym<nvrtcResult (*)(nvrtcProgram, size_t *)>(nvrtc, "nvrtcGetPTXSize");
  auto nvrtcGetPTX = loadSym<nvrtcResult (*)(nvrtcProgram, char *)>(nvrtc, "nvrtcGetPTX");
  auto nvrtcGetProgramLogSize = loadSym<nvrtcResult (*)(nvrtcProgram, size_t *)>(nvrtc, "nvrtcGetProgramLogSize");
  auto nvrtcGetProgramLog = loadSym<nvrtcResult (*)(nvrtcProgram, char *)>(nvrtc, "nvrtcGetProgramLog");

  if (!cuInit || !nvrtcCompileProgram || !cuLaunchKernel) {
    if (error) {
      *error = "CUDA-Symbole fehlen.";
    }
    return false;
  }
  if (cuInit(0) != 0) {
    if (error) {
      *error = "cuInit fehlgeschlagen.";
    }
    return false;
  }
  nvrtcProgram prog = nullptr;
  if (nvrtcCreateProgram(&prog, source, "kernel.cu", 0, nullptr, nullptr) != 0) {
    if (error) {
      *error = "nvrtcCreateProgram fehlgeschlagen.";
    }
    return false;
  }
  char const *opts[] = {"--gpu-architecture=compute_75"};
  if (nvrtcCompileProgram(prog, 1, opts) != 0) {
    size_t logSize = 0;
    nvrtcGetProgramLogSize(prog, &logSize);
    std::string log(logSize, '\0');
    nvrtcGetProgramLog(prog, log.data());
    nvrtcDestroyProgram(&prog);
    if (error) {
      *error = log;
    }
    return false;
  }
  size_t ptxSize = 0;
  nvrtcGetPTXSize(prog, &ptxSize);
  std::vector<char> ptx(ptxSize);
  nvrtcGetPTX(prog, ptx.data());
  nvrtcDestroyProgram(&prog);

  CUdevice dev = 0;
  CUcontext ctx = nullptr;
  cuDeviceGet(&dev, 0);
  cuCtxCreate(&ctx, 0, dev);
  CUmodule mod = nullptr;
  if (cuModuleLoadData(&mod, ptx.data()) != 0) {
    cuCtxDestroy(ctx);
    if (error) {
      *error = "cuModuleLoadData fehlgeschlagen.";
    }
    return false;
  }
  CUfunction fn = nullptr;
  if (cuModuleGetFunction(&fn, mod, "process") != 0) {
    cuModuleUnload(mod);
    cuCtxDestroy(ctx);
    if (error) {
      *error = "CUDA-Kernel 'process' fehlt.";
    }
    return false;
  }

  int const pixels = width * height;
  size_t const bytes = static_cast<size_t>(pixels) * 4u;
  CUdeviceptr dIn = 0;
  CUdeviceptr dOut = 0;
  cuMemAlloc(&dIn, bytes);
  cuMemAlloc(&dOut, bytes);
  cuMemcpyHtoD(dIn, input, bytes);
  void *args[] = {&dIn, &dOut, const_cast<int *>(&pixels)};
  unsigned int const block = 256;
  unsigned int const grid = (static_cast<unsigned int>(pixels) + block - 1u) / block;
  cuLaunchKernel(fn, grid, 1, 1, block, 1, 1, 0, nullptr, args, nullptr);
  cuCtxSynchronize();
  cuMemcpyDtoH(output, dOut, bytes);
  cuMemFree(dIn);
  cuMemFree(dOut);
  cuModuleUnload(mod);
  cuCtxDestroy(ctx);
  return true;
#else
  (void)source;
  (void)input;
  (void)output;
  (void)width;
  (void)height;
  if (error) {
    *error = "CUDA-Launch nur unter Windows.";
  }
  return false;
#endif
}
