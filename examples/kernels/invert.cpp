#include "nr_cpu_abi.h"

int nr_process(NrCpuImage const *in, NrCpuImage *out)
{
  if (!in || !out || !in->rgba || !out->rgba) {
    return 1;
  }
  if (out->width != in->width || out->height != in->height) {
    return 2;
  }
  for (int y = 0; y < in->height; ++y) {
    unsigned char const *src = in->rgba + y * in->stride;
    unsigned char *dst = out->rgba + y * out->stride;
    for (int x = 0; x < in->width; ++x) {
      dst[x * 4 + 0] = (unsigned char)(255 - src[x * 4 + 0]);
      dst[x * 4 + 1] = (unsigned char)(255 - src[x * 4 + 1]);
      dst[x * 4 + 2] = (unsigned char)(255 - src[x * 4 + 2]);
      dst[x * 4 + 3] = src[x * 4 + 3];
    }
  }
  return 0;
}
