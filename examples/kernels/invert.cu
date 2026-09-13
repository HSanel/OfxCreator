extern "C" __global__ void process(unsigned char *in, unsigned char *out, int pixels)
{
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= pixels) {
    return;
  }
  int b = i * 4;
  out[b + 0] = (unsigned char)(255 - in[b + 0]);
  out[b + 1] = (unsigned char)(255 - in[b + 1]);
  out[b + 2] = (unsigned char)(255 - in[b + 2]);
  out[b + 3] = in[b + 3];
}
