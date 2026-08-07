#ifndef FASTMATH_H
#define FASTMATH_H

#include <stdint.h>

float fm_expf(float x);

// 1 / (1 + e^-z).
float fm_sigmoidf(float z);

#endif
