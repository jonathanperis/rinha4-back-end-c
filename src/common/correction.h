#ifndef RINHA_CORRECTION_H
#define RINHA_CORRECTION_H

#include <stddef.h>
#include <stdint.h>

int rinha_current_corpus_correction(const char *body, size_t len, uint8_t *fraud);

#endif
