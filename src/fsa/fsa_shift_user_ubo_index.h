#ifndef FSA_SHIFT_USER_UBO_INDEX_H
#define FSA_SHIFT_USER_UBO_INDEX_H

#include <stdint.h>
#include "nir.h"

bool fsa_shift_user_ubo_index(nir_shader *shader, uint32_t shamt);

#endif