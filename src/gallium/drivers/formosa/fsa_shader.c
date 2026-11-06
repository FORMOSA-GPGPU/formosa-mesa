#include "fsa_shader.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "compiler/glsl_types.h"
#include "compiler/nir/nir.h"
#include "nir/nir.h"
#include "fsa_compiler.h"
#include "fsa_context.h"
#include "fsa_debug.h"
#include "fsa_util.h"

static void
print_nir(nir_shader *nir, const char *path)
{
   FILE *fp = path ? fopen(path, "w") : stderr;
   if (!fp)
      return;
   nir_print_shader(nir, fp);
   if (path)
      fclose(fp);
}

static USCSimKernel *
create_kernel_from_bin(void *bin, size_t size)
{
   char tmp_name[] = "/tmp/fsa_shader_XXXXXX";
   int fd = mkstemp(tmp_name);
   if (fd < 0)
      return NULL;
   if (write(fd, bin, size) != size) {
      close(fd);
      unlink(tmp_name);
      return NULL;
   }
   close(fd);

   USCSimKernel *kernel = uscsim_create_kernel(tmp_name, 0, 0, 0, 0);
   unlink(tmp_name);
   return kernel;
}

static void
collect_shader_info(struct fsa_shader_state *fsa, nir_shader *nir)
{
   memset(fsa->varying_slot_stride, 0, sizeof(fsa->varying_slot_stride));

   uint64_t seen = 0;
   nir_foreach_shader_out_variable (var, nir) {
      int location = var->data.location;
      if (location < VARYING_SLOT_VAR0 || location > VARYING_SLOT_VAR31)
         continue;
      int var_id = location - VARYING_SLOT_VAR0;

      const struct glsl_type *type = var->type;
      enum glsl_base_type base_type = glsl_get_base_type(type);
      unsigned component = glsl_get_components(type);
      unsigned bytes = glsl_base_type_get_bit_size(base_type) * component / 8;
      fsa->varying_slot_stride[var_id] += bytes;

      seen |= (1ULL << var_id);
   }
   fsa->active_varying_slot = seen;
}

void *
fsa_create_vs_state(struct pipe_context *pctx,
                    const struct pipe_shader_state *templat)
{
   struct fsa_shader_state *state = CALLOC_STRUCT(fsa_shader_state);
   if (!state)
      return NULL;
   state->base = *templat;
   nir_shader *nir = nir_shader_clone(NULL, templat->ir.nir);
   /* Collect info before preprocess because out_var will be lowered to
    * load/store */
   collect_shader_info(state, nir);

   fsa_preprocess_nir(nir);
   fsa_compile_nir_opt(nir);

   if (FSA_DEBUG & FSA_DEBUG_NIR) {
      print_nir(nir, getenv("FSA_VS_NIR_DUMP"));
   }

   LLVMModuleRef mod = fsa_nir_to_llvm(nir, false);
   fsa_llvm_verify_optimize_module(mod, NULL);

   if (FSA_DEBUG & FSA_DEBUG_ASM) {
      const char *prefix = getenv("FSA_VS_ASM_DUMP");
      fsa_dump_llvm_asm(mod, prefix ? prefix : "/tmp/fsa_vs");
   }

   fsa_assemble_llvm_module_to_mem(mod, &state->compiled_bin, &state->bin_size);
   state->kernel = create_kernel_from_bin(state->compiled_bin, state->bin_size);
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

   if (FSA_DEBUG & FSA_DEBUG_NIR) {
      print_nir(nir, getenv("FSA_FS_NIR_DUMP"));
   }

   LLVMModuleRef mod = fsa_nir_to_llvm(nir, false);
   fsa_llvm_verify_optimize_module(mod, NULL);

   if (FSA_DEBUG & FSA_DEBUG_ASM) {
      const char *prefix = getenv("FSA_FS_ASM_DUMP");
      fsa_dump_llvm_asm(mod, prefix ? prefix : "/tmp/fsa_fs");
   }

   fsa_assemble_llvm_module_to_mem(mod, &state->compiled_bin, &state->bin_size);
   state->kernel = create_kernel_from_bin(state->compiled_bin, state->bin_size);
   LLVMDisposeModule(mod);
   ralloc_free(nir);
   return (void *)state;
}

void
fsa_bind_vs_state(struct pipe_context *pctx, void *hwcso)
{
   FSA_DBG(FSA_DEBUG_SHADER, "fsa_bind_vs_state\n");
   struct fsa_context *fctx = (struct fsa_context *)pctx;
   fctx->vs = hwcso;
}

void
fsa_delete_vs_state(struct pipe_context *pctx, void *hwcso)
{
   FSA_DBG(FSA_DEBUG_SHADER, "fsa_delete_vs_state\n");
   struct fsa_shader_state *state = fsa_shader_state(hwcso);
   if (state->kernel)
      uscsim_kernel_destroy(state->kernel);
   FREE(state->compiled_bin);
   FREE(hwcso);
}

void
fsa_bind_fs_state(struct pipe_context *pctx, void *hwcso)
{
   FSA_DBG(FSA_DEBUG_SHADER, "fsa_bind_fs_state\n");
   struct fsa_context *fctx = (struct fsa_context *)pctx;
   fctx->fs = hwcso;
}

void
fsa_delete_fs_state(struct pipe_context *pctx, void *hwcso)
{
   FSA_DBG(FSA_DEBUG_SHADER, "fsa_delete_fs_state\n");
   struct fsa_shader_state *state = fsa_shader_state(hwcso);
   if (state->kernel)
      uscsim_kernel_destroy(state->kernel);
   free(state->compiled_bin);
   FREE(hwcso);
}