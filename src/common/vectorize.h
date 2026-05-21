#ifndef RINHA_VECTORIZE_H
#define RINHA_VECTORIZE_H

#include <stddef.h>
#include <stdint.h>

#include "common/distance.h"

int rinha_vectorize(const char *body, size_t len, int16_t out[RINHA_DIMS]);

#endif
