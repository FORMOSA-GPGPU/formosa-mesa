#ifndef FSA_SPIKE_HELPER_H
#define FSA_SPIKE_HELPER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SpikeMachine SpikeMachine;

SpikeMachine *spike_create(const char *elf_name,
                           uint64_t max_memory);
void spike_destroy(SpikeMachine *machine);

int spike_write_memory(SpikeMachine *machine, uint64_t dst,
                       const void *src, size_t len);
int spike_read_memory(SpikeMachine *machine, void *dst, uint64_t src,
                      size_t len);
int spike_run(SpikeMachine *machine);

#ifdef __cplusplus
}
#endif

#endif