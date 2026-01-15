#ifndef FSA_NIR_TO_LLVM_H
#define FSA_NIR_TO_LLVM_H

#include "compiler/nir/nir.h"
#include <llvm-c/Core.h>
#include "fsa_llvm_build.h"

bool fsa_nir_translate(struct fsa_llvm_context *ctx, struct nir_shader *nir);
void fsa_print_llvm_module(LLVMModuleRef module, FILE *fp);

#endif
