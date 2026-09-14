#pragma once

#include <string>

bool nrGpuLaunchCuda(char const *source, unsigned char const *input, unsigned char *output, int width, int height,
                     std::string *error);
bool nrGpuLaunchOpenCl(char const *source, unsigned char const *input, unsigned char *output, int width, int height,
                       std::string *error);
