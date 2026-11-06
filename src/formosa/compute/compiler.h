/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: MIT
 */

#ifndef FSA_COMPUTE_COMPILER_H
#define FSA_COMPUTE_COMPILER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct fsa_compute_binary {
   uint32_t local_size[3];
   uint32_t shared_size;
   uint32_t scratch_size;
   bool uses_printf;
   void *data;
   size_t size;
};

struct nir_spirv_specialization;

bool fsa_compute_compile(const uint32_t *words, size_t word_count,
                         const char *entry,
                         struct nir_spirv_specialization *specializations,
                         unsigned specialization_count,
                         struct fsa_compute_binary *binary);

#endif
