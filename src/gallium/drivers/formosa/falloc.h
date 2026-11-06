#ifndef FALLOC_H
#define FALLOC_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void falloc_init(uintptr_t start, size_t size, FILE *log);
int falloc_malloc(void **devPtr, size_t size);
int falloc_addr_malloc(uintptr_t addr, size_t size);
int falloc_free(void *devPtr);
void falloc_clean(void);

#endif
