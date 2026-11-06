/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: MIT
 */

#ifndef FSA_COMPUTE_H
#define FSA_COMPUTE_H

#include <stdint.h>
#include "compiler/nir/nir.h"
#include "fsa_llvm_build.h"
#include "pocl_image_types.h"

#define FSA_COMPUTE_MAX_SETS     4
#define FSA_COMPUTE_MAX_BINDINGS 32
#define FSA_COMPUTE_MAX_BUFFERS  128
#define FSA_COMPUTE_PUSH_SIZE    128
#define FSA_COMPUTE_PRINTF_SIZE  (16 * 1024 * 1024)

/* Private shader/driver ABI, carried by WGInfo.kargs. */
struct fsa_compute_buffer {
   uint64_t address;
   uint64_t size;
};

struct fsa_compute_args {
   struct fsa_compute_buffer buffers[FSA_COMPUTE_MAX_BUFFERS];
   uint32_t bindings[FSA_COMPUTE_MAX_SETS][FSA_COMPUTE_MAX_BINDINGS];
   uint32_t base_group[3];
   dev_image_t images[FSA_COMPUTE_MAX_BUFFERS];
   uint32_t samplers[FSA_COMPUTE_MAX_BUFFERS];
   uint64_t printf_address;
   uint64_t scratch_address;
   uint32_t scratch_stride;
   uint8_t push[FSA_COMPUTE_PUSH_SIZE];
};

bool fsa_compute_emit_intrinsic(struct fsa_llvm_context *ctx,
                                nir_intrinsic_instr *instr, LLVMValueRef *src,
                                LLVMValueRef *result);
LLVMValueRef fsa_compute_emit_deref(struct fsa_llvm_context *ctx,
                                    nir_deref_instr *deref, LLVMValueRef parent,
                                    LLVMValueRef index);
bool fsa_compute_emit_tex(struct fsa_llvm_context *ctx, nir_tex_instr *tex,
                          LLVMValueRef *src, LLVMValueRef *result);

#endif
