#ifndef RINHA_SEARCH_H
#define RINHA_SEARCH_H

#include <stdint.h>

#include "common/distance.h"
#include "common/index.h"

uint8_t rinha_search_fraud_count(const rinha_index_t *index, const int16_t query[RINHA_DIMS]);

#endif
