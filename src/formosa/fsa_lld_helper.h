#ifndef LLD_WRAPPER_H
#define LLD_WRAPPER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool fsa_lld_link_elf(int argc, const char** argv, char** out_msg);

void fsa_lld_free_msg(char* msg);

#ifdef __cplusplus
}
#endif

#endif