/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: MIT
 */

#include "compiler.h"
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "c11/threads.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/spirv/nir_spirv.h"
#include "util/u_printf.h"
#include <llvm-c/Analysis.h>
#include <llvm-c/BitReader.h>
#include <llvm-c/Error.h>
#include <llvm-c/Linker.h>
#include <llvm-c/Support.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <sys/wait.h>
#include "fsa_compute.h"
#include "fsa_nir_to_llvm.h"
#include "fsa_util.h"
#include "start.h"

extern char **environ;

static once_flag target_once = ONCE_FLAG_INIT;

static void
init_compute_target(void)
{
   fsa_llvm_riscv_init();
   /* Reuse Formosa's existing divergence pass so lanes reconverge at loop
    * iterations and subgroup operations observe the same dynamic instance. */
   const char *args[] = {"formosa-compute", "-fsa-ipdom-like"};
   LLVMParseCommandLineOptions(ARRAY_SIZE(args), args, NULL);
}

static const nir_shader_compiler_options options = {
   .lower_fdiv = true,
   .lower_fmod = true,
   .lower_flrp32 = true,
   .lower_fpow = true,
   .lower_extract_byte = true,
   .lower_extract_word = true,
   .lower_insert_byte = true,
   .lower_insert_word = true,
};

static void
shared_layout(const struct glsl_type *type, unsigned *size, unsigned *align)
{
   glsl_get_natural_size_align_bytes(type, size, align);
}

static void
prepare_nir(nir_shader *nir)
{
   NIR_PASS_V(nir, nir_lower_variable_initializers, nir_var_function_temp);
   NIR_PASS_V(nir, nir_lower_returns);
   NIR_PASS_V(nir, nir_inline_functions);
   nir_remove_non_entrypoints(nir);
   NIR_PASS_V(nir, nir_lower_system_values);
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_var_copies);
   NIR_PASS_V(nir, nir_lower_continue_constructs);
   /* Preserve ordered NaN comparisons before constant votes fold to true. */
   const nir_lower_subgroups_options vote_options = {
      .ballot_bit_size = 32,
      .ballot_components = 4,
      .lower_vote_eq = true,
      .lower_vote_bool_eq = true,
   };
   NIR_PASS_V(nir, nir_lower_subgroups, &vote_options);
   bool normalized;
   do {
      normalized = false;
      NIR_PASS(normalized, nir, nir_opt_deref);
      NIR_PASS(normalized, nir, nir_lower_vars_to_ssa);
      NIR_PASS(normalized, nir, nir_copy_prop);
      NIR_PASS(normalized, nir, nir_opt_constant_folding);
      NIR_PASS(normalized, nir, nir_opt_algebraic);
      NIR_PASS(normalized, nir, nir_opt_dead_cf);
      NIR_PASS(normalized, nir, nir_opt_if, 0);
      NIR_PASS(normalized, nir, nir_opt_loop);
      NIR_PASS(normalized, nir, nir_opt_loop_unroll);
      NIR_PASS(normalized, nir, nir_opt_peephole_select, 16, false, false);
      NIR_PASS(normalized, nir, nir_opt_dce);
   } while (normalized);
   const nir_lower_subgroups_options subgroup_options = {
      .ballot_bit_size = 64,
      .ballot_components = 1,
      .lower_to_scalar = true,
      .lower_subgroup_masks = true,
      .lower_inverse_ballot = true,
      .lower_first_invocation_to_ballot = true,
   };
   NIR_PASS_V(nir, nir_lower_subgroups, &subgroup_options);
   const nir_lower_tex_options tex_options = {.lower_txp = ~0u};
   NIR_PASS_V(nir, nir_lower_tex, &tex_options);
   const nir_lower_printf_options printf_options = {
      .max_buffer_size = FSA_COMPUTE_PRINTF_SIZE,
      .ptr_bit_size = 64,
      .hash_format_strings = true,
   };
   NIR_PASS_V(nir, nir_lower_printf, &printf_options);
   NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_global,
              nir_address_format_64bit_global);
   NIR_PASS_V(nir, nir_split_struct_vars, nir_var_function_temp);
   NIR_PASS_V(nir, nir_lower_vars_to_explicit_types, nir_var_mem_shared,
              shared_layout);
   NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_shared,
              nir_address_format_32bit_offset);
   NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_push_const,
              nir_address_format_32bit_offset);
   NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_ssbo | nir_var_mem_ubo,
              nir_address_format_32bit_index_offset);
   NIR_PASS_V(nir, nir_lower_global_vars_to_local);
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_var_copies);
   NIR_PASS_V(nir, nir_lower_vars_to_ssa);
   NIR_PASS_V(nir, nir_lower_vars_to_scratch, nir_var_function_temp, 0,
              shared_layout, shared_layout);
   NIR_PASS_V(nir, nir_lower_alu_to_scalar, NULL, NULL);
   bool progress;
   do {
      progress = false;
      NIR_PASS(progress, nir, nir_lower_vars_to_ssa);
      NIR_PASS(progress, nir, nir_copy_prop);
      NIR_PASS(progress, nir, nir_opt_deref);
      NIR_PASS(progress, nir, nir_opt_dce);
      NIR_PASS(progress, nir, nir_opt_dead_cf);
      NIR_PASS(progress, nir, nir_opt_cse);
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_constant_folding);
   } while (progress);
   NIR_PASS_V(nir, nir_lower_load_const_to_scalar);
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   nir_validate_shader(nir, "Formosa compute");
}

static bool
write_bytes(const char *path, const void *data, size_t size)
{
   FILE *file = fopen(path, "wb");
   if (!file)
      return false;
   bool ok = fwrite(data, 1, size, file) == size;
   return fclose(file) == 0 && ok;
}

static bool
optimize_module(struct fsa_llvm_context *ctx)
{
   LLVMPassBuilderOptionsRef options = LLVMCreatePassBuilderOptions();
   LLVMErrorRef error =
      LLVMRunPasses(ctx->module, "globaldce,default<O2>", NULL, options);
   LLVMDisposePassBuilderOptions(options);
   if (error) {
      char *message = LLVMGetErrorMessage(error);
      fprintf(stderr, "Formosa compute: %s\n", message);
      LLVMDisposeErrorMessage(message);
      return false;
   }
   return true;
}

static bool
link_device_library(struct fsa_llvm_context *ctx)
{
   bool needed = false;
   for (LLVMValueRef f = LLVMGetFirstFunction(ctx->module); f;
        f = LLVMGetNextFunction(f)) {
      const char *name = LLVMGetValueName(f);
      if (LLVMIsDeclaration(f) && strncmp(name, "_Z", 2) == 0)
         needed = true;
   }
   if (!needed)
      return true;
   LLVMMemoryBufferRef bytes;
   LLVMModuleRef library;
   char *message = NULL;
   if (LLVMCreateMemoryBufferWithContentsOfFile(FORMOSA_KERNEL, &bytes,
                                                &message)) {
      fprintf(stderr, "Formosa compute: %s\n", message);
      LLVMDisposeMessage(message);
      return false;
   }
   bool ok = LLVMParseBitcodeInContext2(ctx->context, bytes, &library) == 0;
   LLVMDisposeMemoryBuffer(bytes);
   if (!ok || LLVMLinkModules2(ctx->module, library))
      return false;
   for (LLVMValueRef f = LLVMGetFirstFunction(ctx->module); f;
        f = LLVMGetNextFunction(f))
      if (!LLVMIsDeclaration(f) && strcmp(LLVMGetValueName(f), "main") != 0) {
         LLVMSetLinkage(f, LLVMInternalLinkage);
         /* PoCL's library bitcode deliberately preserves builtin bodies. As
          * in its kernel compilation path, optimize them after linking. */
         LLVMRemoveEnumAttributeAtIndex(
            f, LLVMAttributeFunctionIndex,
            LLVMGetEnumAttributeKindForName("optnone", 7));
         LLVMRemoveEnumAttributeAtIndex(
            f, LLVMAttributeFunctionIndex,
            LLVMGetEnumAttributeKindForName("noinline", 8));
      }
   for (LLVMValueRef g = LLVMGetFirstGlobal(ctx->module); g;
        g = LLVMGetNextGlobal(g))
      if (!LLVMIsDeclaration(g))
         LLVMSetLinkage(g, LLVMInternalLinkage);
   return true;
}

bool
fsa_compute_compile(const uint32_t *words, size_t word_count, const char *entry,
                    struct nir_spirv_specialization *specializations,
                    unsigned specialization_count,
                    struct fsa_compute_binary *binary)
{
   memset(binary, 0, sizeof(*binary));
   const struct spirv_to_nir_options spirv_options = {
      .environment = NIR_SPIRV_VULKAN,
      .printf = true,
      .global_addr_format = nir_address_format_64bit_global,
      .ubo_addr_format = nir_address_format_32bit_index_offset,
      .ssbo_addr_format = nir_address_format_32bit_index_offset,
      .push_const_addr_format = nir_address_format_logical,
      .shared_addr_format = nir_address_format_32bit_offset,
   };
   glsl_type_singleton_init_or_ref();
   nir_shader *nir =
      spirv_to_nir(words, word_count, specializations, specialization_count,
                   MESA_SHADER_COMPUTE, entry, &spirv_options, &options);
   if (!nir) {
      glsl_type_singleton_decref();
      return false;
   }
   prepare_nir(nir);
   if (getenv("FSA_COMPUTE_DUMP_NIR"))
      nir_print_shader(nir, stderr);
   for (unsigned i = 0; i < 3; i++)
      binary->local_size[i] = nir->info.workgroup_size[i];
   binary->shared_size = nir->info.shared_size;
   binary->scratch_size = ALIGN_POT(nir->scratch_size, 16);
   binary->uses_printf = nir->printf_info_count != 0;
   u_printf_singleton_init_or_ref();
   if (nir->printf_info_count)
      u_printf_singleton_add(nir->printf_info, nir->printf_info_count);

   struct fsa_llvm_context ctx;
   fsa_llvm_context_init(&ctx);
   ctx.is_cpu = false;
   bool ok = fsa_nir_translate(&ctx, nir);
   if (ok) {
      nir_function *nir_entry = nir_shader_get_entrypoint(nir)->function;
      LLVMValueRef function = LLVMGetNamedFunction(
         ctx.module, nir_entry->name ? nir_entry->name : "unknown");
      LLVMSetValueName2(function, "main", 4);
      LLVMBasicBlockRef block = LLVMGetInsertBlock(ctx.builder);
      if (!LLVMGetBasicBlockTerminator(block))
         LLVMBuildRetVoid(ctx.builder);
      ok = link_device_library(&ctx) && optimize_module(&ctx);
      if (ok)
         ok = fsa_llvm_verify_module(ctx.module) == 0;
   }
   if (getenv("FSA_COMPUTE_DUMP_LLVM"))
      LLVMPrintModuleToFile(ctx.module, getenv("FSA_COMPUTE_DUMP_LLVM"), NULL);
   u_printf_singleton_decref();
   ralloc_free(nir);
   glsl_type_singleton_decref();

   call_once(&target_once, init_compute_target);
   LLVMTargetRef target;
   LLVMTargetMachineRef machine = NULL;
   LLVMMemoryBufferRef object = NULL;
   char *error = NULL;
   if (ok &&
       LLVMGetTargetFromTriple("riscv64-unknown-elf", &target, &error) == 0) {
      machine = LLVMCreateTargetMachine(
         target, "riscv64-unknown-elf", "formosa-gpgpu", "",
         LLVMCodeGenLevelDefault, LLVMRelocPIC, LLVMCodeModelSmall);
      LLVMSetTarget(ctx.module, "riscv64-unknown-elf");
      ok = machine &&
           LLVMTargetMachineEmitToMemoryBuffer(
              machine, ctx.module, LLVMObjectFile, &error, &object) == 0;
   } else {
      ok = false;
   }
   if (error) {
      fprintf(stderr, "Formosa compute: %s\n", error);
      LLVMDisposeMessage(error);
   }
   LLVMDisposeBuilder(ctx.builder);
   LLVMDisposeModule(ctx.module);
   LLVMContextDispose(ctx.context);
   free(ctx.flow->stack);
   free(ctx.flow);
   if (machine)
      LLVMDisposeTargetMachine(machine);
   if (!ok)
      return false;

   char directory[] = "/tmp/formosa-shader-XXXXXX";
   if (!mkdtemp(directory)) {
      LLVMDisposeMemoryBuffer(object);
      return false;
   }
   char shader_path[128], start_path[128], elf_path[128];
   snprintf(shader_path, sizeof(shader_path), "%s/shader.o", directory);
   snprintf(start_path, sizeof(start_path), "%s/start.o", directory);
   snprintf(elf_path, sizeof(elf_path), "%s/shader.elf", directory);
   ok = write_bytes(shader_path, LLVMGetBufferStart(object),
                    LLVMGetBufferSize(object)) &&
        write_bytes(start_path, fsa_compute_start, sizeof(fsa_compute_start));
   LLVMDisposeMemoryBuffer(object);
   if (ok) {
      char *argv[] = {FORMOSA_CLANG,
                      "-nostartfiles",
                      "-mcpu=formosa-gpgpu",
                      "-mabi=lp64",
                      "-Wl,-pie,-Bsymbolic,--no-relax,-e,_start",
                      start_path,
                      shader_path,
                      "-lm",
                      "-o",
                      elf_path,
                      NULL};
      pid_t pid;
      int status = 0;
      ok = posix_spawn(&pid, FORMOSA_CLANG, NULL, NULL, argv, environ) == 0;
      if (ok)
         ok = waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
              WEXITSTATUS(status) == 0;
   }
   if (ok) {
      FILE *file = fopen(elf_path, "rb");
      ok = file && fseek(file, 0, SEEK_END) == 0;
      long length = ok ? ftell(file) : -1;
      if (length <= 0 || fseek(file, 0, SEEK_SET) != 0)
         ok = false;
      if (ok) {
         binary->data = malloc(length);
         binary->size = length;
         ok = binary->data &&
              fread(binary->data, 1, length, file) == (size_t)length;
      }
      if (file)
         fclose(file);
   }
   unlink(shader_path);
   unlink(start_path);
   unlink(elf_path);
   rmdir(directory);
   if (!ok) {
      free(binary->data);
      memset(binary, 0, sizeof(*binary));
   }
   return ok;
}
