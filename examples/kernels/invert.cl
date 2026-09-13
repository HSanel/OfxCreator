__kernel void process(__global const uchar *in, __global uchar *out, const int pixels)
{
  int i = get_global_id(0);
  if (i >= pixels) {
    return;
  }
  int b = i * 4;
  out[b + 0] = (uchar)(255 - in[b + 0]);
  out[b + 1] = (uchar)(255 - in[b + 1]);
  out[b + 2] = (uchar)(255 - in[b + 2]);
  out[b + 3] = in[b + 3];
}
