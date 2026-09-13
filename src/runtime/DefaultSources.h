#pragma once

#include "ir/NodeKind.h"

#include <QString>

inline QString defaultKernelSource(NodeKind kind)
{
  switch (kind) {
  case NodeKind::Cpu:
    return QStringLiteral(
      "#include \"nr_cpu_abi.h\"\n"
      "\n"
      "int nr_process(NrCpuImage const *in, NrCpuImage *out)\n"
      "{\n"
      "  if (!in || !out || !in->rgba || !out->rgba) {\n"
      "    return 1;\n"
      "  }\n"
      "  if (out->width != in->width || out->height != in->height) {\n"
      "    return 2;\n"
      "  }\n"
      "  for (int y = 0; y < in->height; ++y) {\n"
      "    unsigned char const *src = in->rgba + y * in->stride;\n"
      "    unsigned char *dst = out->rgba + y * out->stride;\n"
      "    for (int x = 0; x < in->width; ++x) {\n"
      "      dst[x * 4 + 0] = (unsigned char)(255 - src[x * 4 + 0]);\n"
      "      dst[x * 4 + 1] = (unsigned char)(255 - src[x * 4 + 1]);\n"
      "      dst[x * 4 + 2] = (unsigned char)(255 - src[x * 4 + 2]);\n"
      "      dst[x * 4 + 3] = src[x * 4 + 3];\n"
      "    }\n"
      "  }\n"
      "  return 0;\n"
      "}\n");
  case NodeKind::Cuda:
    return QStringLiteral(
      "extern \"C\" __global__ void process(unsigned char *in, unsigned char *out, int pixels)\n"
      "{\n"
      "  int i = blockIdx.x * blockDim.x + threadIdx.x;\n"
      "  if (i >= pixels) {\n"
      "    return;\n"
      "  }\n"
      "  int b = i * 4;\n"
      "  out[b + 0] = (unsigned char)(255 - in[b + 0]);\n"
      "  out[b + 1] = (unsigned char)(255 - in[b + 1]);\n"
      "  out[b + 2] = (unsigned char)(255 - in[b + 2]);\n"
      "  out[b + 3] = in[b + 3];\n"
      "}\n");
  case NodeKind::OpenCl:
    return QStringLiteral(
      "__kernel void process(__global const uchar *in, __global uchar *out, const int pixels)\n"
      "{\n"
      "  int i = get_global_id(0);\n"
      "  if (i >= pixels) {\n"
      "    return;\n"
      "  }\n"
      "  int b = i * 4;\n"
      "  out[b + 0] = (uchar)(255 - in[b + 0]);\n"
      "  out[b + 1] = (uchar)(255 - in[b + 1]);\n"
      "  out[b + 2] = (uchar)(255 - in[b + 2]);\n"
      "  out[b + 3] = in[b + 3];\n"
      "}\n");
  case NodeKind::Python:
    return QStringLiteral(
      "def process(width, height, rgba):\n"
      "    out = bytearray(rgba)\n"
      "    for i in range(0, width * height * 4, 4):\n"
      "        out[i] = 255 - out[i]\n"
      "        out[i + 1] = 255 - out[i + 1]\n"
      "        out[i + 2] = 255 - out[i + 2]\n"
      "    return out\n");
  default:
    break;
  }
  return {};
}
