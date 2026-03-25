#include "fsa_shader.h"
#include "fsa_compiler.h"
#include "fsa_util.h"

void *
fsa_create_vs_state(struct pipe_context *pctx,
                    const struct pipe_shader_state *templat)
{
   struct fsa_shader_state *state = CALLOC_STRUCT(fsa_shader_state);
   if (!state)
      return NULL;
   state->base = *templat;
   nir_shader *nir = nir_shader_clone(NULL, templat->ir.nir);
   fsa_preprocess_nir(nir);
   fsa_compile_nir_opt(nir);
   LLVMModuleRef mod = fsa_nir_to_llvm(nir, false);
   fsa_llvm_verify_optimize_module(mod, NULL);
   fsa_assemble_llvm_module_to_mem(mod, &state->compiled_bin, &state->bin_size);
   LLVMDisposeModule(mod);
   ralloc_free(nir);
   return (void *)state;
}

void *
fsa_create_fs_state(struct pipe_context *pctx,
                    const struct pipe_shader_state *templat)
{
   struct fsa_shader_state *state = CALLOC_STRUCT(fsa_shader_state);
   if (!state)
      return NULL;
   state->base = *templat;
   nir_shader *nir = nir_shader_clone(NULL, templat->ir.nir);
   fsa_preprocess_nir(nir);
   fsa_compile_nir_opt(nir);
   LLVMModuleRef mod = fsa_nir_to_llvm(nir, false);
   fsa_llvm_verify_optimize_module(mod, NULL);
   fsa_assemble_llvm_module_to_mem(mod, &state->compiled_bin, &state->bin_size);
   LLVMDisposeModule(mod);
   ralloc_free(nir);
   return (void *)state;
}