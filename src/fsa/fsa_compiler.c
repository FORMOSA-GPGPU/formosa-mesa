#include "fsa_compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler/glsl/builtin_functions.h"
#include "compiler/glsl/gl_nir_linker.h"
#include "compiler/glsl/glsl_parser_extras.h"
#include "compiler/glsl/linker_util.h"
#include "compiler/glsl/standalone.h"
#include "compiler/nir/nir.h"
#include "main/mtypes.h"
#include "main/shader_types.h"
#include "main/shaderapi.h"
#include "main/shaderobj.h"
#include <llvm-c/Analysis.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include "argparse.h"
#include "fsa_llvm_build.h"
#include "fsa_llvm_verify.h"
#include "fsa_lower_viewport_to_ubo.h"
#include "fsa_lower_viewport_transform.h"
#include "fsa_nir_to_llvm.h"
#include "fsa_util.h"

void initialize_context_to_defaults(struct gl_context *ctx, gl_api api);
struct gl_shader_program *standalone_create_shader_program(void);
void standalone_destroy_shader_program(struct gl_shader_program *whole_program);
struct gl_shader *
standalone_add_shader_source(struct gl_context *ctx,
                             struct gl_shader_program *whole_program,
                             GLenum type, const char *source);

/** @file fsa_compiler.cc
 *
 * Wrapper for standalone compiler lib. Allow future development and study
 * of glsl compiler.
 */

static const struct nir_shader_compiler_options nir_vs_options = {0};
static const struct nir_shader_compiler_options nir_fs_options = {0};

struct standalone_options *
fsa_get_standalone_options(const struct fsa_options *f)
{
   return (struct standalone_options *)f;
}

static GLenum
fsa_get_stage_from_ext(const char *filename)
{
   const char *dot = strrchr(filename, '.');
   const char *ext = dot + 1;
   if (!strcmp(ext, "vert") || !strcmp(ext, "vs"))
      return GL_VERTEX_SHADER;
   else if (!strcmp(ext, "frag") || !strcmp(ext, "fs"))
      return GL_FRAGMENT_SHADER;
   return -1;
}

static char *
fsa_read_file(const char *filename)
{
   FILE *file = fopen(filename, "rb");
   if (!file) {
      perror("Error opening file");
      return NULL;
   }

   if (fseek(file, 0, SEEK_END) != 0) {
      perror("Error seeking to end of file");
      fclose(file);
      return NULL;
   }

   long size = ftell(file);
   if (size == -1) {
      perror("Error getting file size");
      fclose(file);
      return NULL;
   }

   if (fseek(file, 0, SEEK_SET) != 0) {
      perror("Error seeking to start of file");
      fclose(file);
      return NULL;
   }

   char *buffer = (char *)malloc(size + 1);
   if (!buffer) {
      perror("Error allocating memory");
      fclose(file);
      return NULL;
   }

   size_t bytes_read = fread(buffer, 1, size, file);
   if (bytes_read != size) {
      perror("Error reading file");
      free(buffer);
      fclose(file);
      return NULL;
   }

   buffer[size] = '\0';

   fclose(file);
   return buffer;
}

static bool
fsa_preprocess_nir(nir_shader *nir)
{
   bool progress = false;
   /* Move temp global-vars to local */
   NIR_PASS(progress, nir, nir_lower_global_vars_to_local);
   NIR_PASS(progress, nir, nir_lower_var_copies);
   /* lower_viewport_transform matches `store_deref` so it must run after
    * lower_var_copies */
   if (nir->info.stage == MESA_SHADER_VERTEX) {
      NIR_PASS(progress, nir, fsa_lower_viewport_transform);
      NIR_PASS(progress, nir, fsa_lower_viewport_to_ubo);
   }

   NIR_PASS(progress, nir, nir_lower_io_to_scalar, nir_var_mem_ubo, NULL, NULL);
   /* Clean up constant expressions after lowering */
   NIR_PASS(progress, nir, nir_opt_constant_folding);
   NIR_PASS(progress, nir, nir_opt_cse);
   NIR_PASS(progress, nir, nir_opt_dce);
   return progress;
}

static struct gl_shader_program *
fsa_compile_shader(struct gl_context *ctx, unsigned num_files,
                   char *const *files)
{
   bool status = EXIT_SUCCESS;
   struct gl_shader_program *prog = standalone_create_shader_program();
   for (unsigned i = 0; i < num_files; i++) {
      const char *filename = files[i];
      GLenum stage = fsa_get_stage_from_ext(filename);
      if (stage < 0) {
         goto fail;
      }

      char *source = fsa_read_file(filename);

      struct gl_shader *shader =
         standalone_add_shader_source(ctx, prog, stage, source);

      /* Compile */
      _mesa_glsl_compile_shader(ctx, shader, NULL, false, false, true);

      if (strlen(shader->InfoLog) > 0) {
         printf("Info log for %s:\n%s\n", filename, shader->InfoLog);
      }

      if (!shader->CompileStatus) {
         status = EXIT_FAILURE;
         goto fail;
      }
   }

   if (status == EXIT_SUCCESS) {
      /* Link */
      _mesa_clear_shader_program_data(ctx, prog);
      prog->data->LinkStatus = LINKING_SUCCESS;
      link_shaders_init(ctx, prog);
      gl_nir_link_glsl(ctx, prog);
      status = prog->data->LinkStatus ? EXIT_SUCCESS : EXIT_FAILURE;
      if (strlen(prog->data->InfoLog) > 0) {
         printf("Info log for linking %s:\n", prog->data->InfoLog);
         goto fail;
      }
      if (status == EXIT_FAILURE) {
         printf("Link failed\n");
         goto fail;
      }
   }
   return prog;
fail:
   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      if (prog->_LinkedShaders[i])
         _mesa_delete_linked_shader(ctx, prog->_LinkedShaders[i]);
   }

   ralloc_free(prog);
   return NULL;
}

void
fsa_compiler_cleanup(struct gl_shader_program *prog)
{
   standalone_compiler_cleanup(prog);
}

static void
fsa_print_nir_info(struct nir_shader *nir, FILE *fp)
{
   fprintf(fp, "NIR shader info:\n");
   fprintf(fp, "\tNum inputs: %d\n", nir->num_inputs);
   fprintf(fp, "\tNum uniforms: %d\n", nir->num_uniforms);
   fprintf(fp, "\tNum outputs: %d\n", nir->num_outputs);
   fprintf(fp, "\tGlobal mem size: %d\n", nir->global_mem_size);
   fprintf(fp, "\tScratch size: %d\n", nir->scratch_size);
   fprintf(fp, "\tConstant data size: %d\n", nir->constant_data_size);
   fprintf(fp, "\n");
}

static void
fsa_print_linked_prog_nir(struct gl_shader_program *whole_prog, FILE *fp)
{
   struct gl_program *vert_prog =
      whole_prog->_LinkedShaders[MESA_SHADER_VERTEX]->Program;
   struct gl_program *frag_prog =
      whole_prog->_LinkedShaders[MESA_SHADER_FRAGMENT]->Program;
   nir_print_shader(vert_prog->nir, fp);
   fsa_print_nir_info(vert_prog->nir, fp);
   nir_print_shader(frag_prog->nir, fp);
   fsa_print_nir_info(frag_prog->nir, fp);
}

LLVMModuleRef
fsa_nir_to_llvm(nir_shader *shader, int is_cpu)
{
   struct fsa_llvm_context tmp;
   fsa_llvm_context_init(&tmp);
   tmp.is_cpu = is_cpu;
   if (!fsa_nir_translate(&tmp, shader))
      return NULL;
   /* Insert a temp ret to pass verification */
   LLVMBuildRetVoid(tmp.builder);

   if (fsa_llvm_verify_module(tmp.module)) {
      return NULL;
   }
   return tmp.module;
}

const nir_shader_compiler_options fsa_nir_option = {
   .lower_pack_half_2x16 = true,
   .lower_pack_unorm_2x16 = true,
   .lower_pack_snorm_2x16 = true,
   .lower_pack_unorm_4x8 = true,
   .lower_pack_snorm_4x8 = true,
   .lower_pack_64_2x32 = true,
   .lower_pack_64_4x16 = true,
   .lower_pack_32_2x16 = true,
   .lower_pack_64_2x32_split = true,
   .lower_pack_32_2x16_split = true,
   .lower_unpack_half_2x16 = true,
   .lower_unpack_unorm_2x16 = true,
   .lower_unpack_snorm_2x16 = true,
   .lower_unpack_unorm_4x8 = true,
   .lower_unpack_snorm_4x8 = true,
   .lower_unpack_64_2x32_split = true,
   .lower_unpack_32_2x16_split = true,
};

static int
type_size_func(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

void
fsa_compile_nir_opt(nir_shader *shader)
{
   shader->options = &fsa_nir_option;
   bool progress = false;
   NIR_PASS(progress, shader, nir_lower_vars_to_ssa);

   int lower_modes = nir_var_shader_in | nir_var_shader_out | nir_var_uniform;
   NIR_PASS(progress, shader, nir_lower_io, lower_modes, type_size_func,
            (nir_lower_io_options)0);
   NIR_PASS(progress, shader, nir_lower_io_to_scalar, lower_modes, NULL, NULL);

   NIR_PASS(progress, shader, nir_lower_alu_to_scalar, NULL, NULL);
   NIR_PASS(progress, shader, nir_lower_load_const_to_scalar);
   NIR_PASS(progress, shader, nir_opt_cse);
   NIR_PASS(progress, shader, nir_opt_dce);
}

static const char *const compile_usages[] = {
   "fsa_compiler compile [options] <vert> <frag>",
   NULL,
};

static void
assemble_llvm_module(LLVMModuleRef module, const char *prefix)
{
   char triple[] = "riscv64-unknown-elf";
   char cpu[] = "formosa-gpgpu";
   char features[] = "-f,-d";

   char *error = NULL;
   char asm_name[64] = {0};
   sprintf(asm_name, "%s.S", prefix);

   LLVMTargetRef target;
   if (LLVMGetTargetFromTriple(triple, &target, &error) != 0) {
      printf("Error getting target: %s\n", error);
      return;
   }
   LLVMTargetMachineRef target_machine = LLVMCreateTargetMachine(
      target, triple, cpu, features, LLVMCodeGenLevelAggressive,
      LLVMRelocStatic, LLVMCodeModelMedium);

   if (LLVMTargetMachineEmitToFile(target_machine, module, asm_name,
                                   LLVMAssemblyFile, &error) != 0) {
      printf("Error emitting to file :%s\n", error);
      return;
   }
}

static void
initialize_context(struct gl_context *ctx)
{
   initialize_context_to_defaults(ctx, API_OPENGLES2);
   _mesa_glsl_builtin_functions_init_or_ref();
   ctx->Version = 30;
   ctx->Const.ShaderCompilerOptions[MESA_SHADER_VERTEX].NirOptions =
      &nir_vs_options;
   ctx->Const.ShaderCompilerOptions[MESA_SHADER_FRAGMENT].NirOptions =
      &nir_fs_options;
   ctx->Extensions.ARB_ES3_compatibility = true;
   ctx->Extensions.ARB_ES3_1_compatibility = true;
   ctx->Extensions.ARB_ES3_2_compatibility = true;
   ctx->Const.GLSLVersion = 300;

   /* Copied from standalone.cpp, might change in the future */
   ctx->Const.MaxComputeWorkGroupCount[0] = 65535;
   ctx->Const.MaxComputeWorkGroupCount[1] = 65535;
   ctx->Const.MaxComputeWorkGroupCount[2] = 65535;
   ctx->Const.MaxComputeWorkGroupSize[0] = 1024;
   ctx->Const.MaxComputeWorkGroupSize[1] = 1024;
   ctx->Const.MaxComputeWorkGroupSize[2] = 64;
   ctx->Const.MaxComputeWorkGroupInvocations = 1024;
   ctx->Const.MaxComputeSharedMemorySize = 32768;
   ctx->Const.MaxComputeVariableGroupSize[0] = 512;
   ctx->Const.MaxComputeVariableGroupSize[1] = 512;
   ctx->Const.MaxComputeVariableGroupSize[2] = 64;
   ctx->Const.MaxComputeVariableGroupInvocations = 512;
   ctx->Const.Program[MESA_SHADER_COMPUTE].MaxTextureImageUnits = 16;
   ctx->Const.Program[MESA_SHADER_COMPUTE].MaxUniformComponents = 1024;
   ctx->Const.Program[MESA_SHADER_COMPUTE].MaxCombinedUniformComponents = 1024;
   ctx->Const.Program[MESA_SHADER_COMPUTE].MaxInputComponents =
      0; /* not used */
   ctx->Const.Program[MESA_SHADER_COMPUTE].MaxOutputComponents =
      0; /* not used */
   ctx->Const.Program[MESA_SHADER_COMPUTE].MaxAtomicBuffers = 8;
   ctx->Const.Program[MESA_SHADER_COMPUTE].MaxAtomicCounters = 8;
   ctx->Const.Program[MESA_SHADER_COMPUTE].MaxImageUniforms = 8;
   ctx->Const.Program[MESA_SHADER_COMPUTE].MaxUniformBlocks = 12;

   ctx->Const.MaxClipPlanes = 8;
   ctx->Const.MaxCombinedTextureImageUnits = 32;
   ctx->Const.MaxDrawBuffers = 4;
   ctx->Const.MinProgramTexelOffset = -8;
   ctx->Const.MaxProgramTexelOffset = 7;
   ctx->Const.MaxLights = 0;
   ctx->Const.MaxTextureCoordUnits = 0;
   ctx->Const.MaxTextureUnits = 0;
   ctx->Const.MaxUniformBufferBindings = 16;
   ctx->Const.MaxVertexStreams = 4;
   ctx->Const.MaxTransformFeedbackBuffers = 4;

   ctx->Const.Program[MESA_SHADER_VERTEX].MaxAttribs = 16;
   ctx->Const.Program[MESA_SHADER_VERTEX].MaxTextureImageUnits = 16;
   ctx->Const.Program[MESA_SHADER_VERTEX].MaxUniformComponents = 1024;
   ctx->Const.Program[MESA_SHADER_VERTEX].MaxCombinedUniformComponents = 1024;
   ctx->Const.Program[MESA_SHADER_VERTEX].MaxInputComponents = 0; /* not used */
   ctx->Const.Program[MESA_SHADER_VERTEX].MaxOutputComponents = 16 * 4;

   ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxTextureImageUnits = 16;
   ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxUniformComponents = 224;
   ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxCombinedUniformComponents = 224;
   ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxInputComponents = 15 * 4;
   ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxOutputComponents =
      0; /* not used */

   ctx->Const.MaxVarying =
      ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxInputComponents / 4;

   ctx->Const.GenerateTemporaryNames = true;
   ctx->Const.MaxPatchVertices = 32;
}

int
cmd_compile(int argc, const char **argv)
{
   const char *prefix = NULL;
   struct argparse_option options_arg[] = {
      OPT_HELP(),
      OPT_STRING('p', "prefix", &prefix, "Output prefix", NULL, 0, 0),
      OPT_END(),
   };

   struct argparse argparse;
   argparse_init(&argparse, options_arg, compile_usages, 0);
   argparse_describe(&argparse, "\nCompile shader", "");

   argc = argparse_parse(&argparse, argc, argv);

   if (!prefix) {
      printf("Error: Output prefix is required.\n");
      return -1;
   }

   if (argc < 2) {
      argparse_usage(&argparse);
      return -1;
   }

   struct gl_context ctx = {0};
   initialize_context(&ctx);

   struct gl_shader_program *whole_program =
      fsa_compile_shader(&ctx, 2, (char *const *)argv);

   if (!whole_program) {
      return -1;
   }

   char file_nir[64] = {0};
   char file_nir_opt[64] = {0};
   char file_vs_llvm[64] = {0};
   char file_vs_llvm_opt[64] = {0};
   char file_fs_llvm[64] = {0};
   char file_fs_llvm_opt[64] = {0};
   char file_vs_object[64] = {0};
   char file_fs_object[64] = {0};
   snprintf(file_nir, 63, "%s.nir", prefix);
   snprintf(file_nir_opt, 63, "%s.nir.opt", prefix);
   snprintf(file_vs_llvm, 63, "%s.vs.ll", prefix);
   snprintf(file_vs_llvm_opt, 63, "%s.vs.opt.ll", prefix);
   snprintf(file_fs_llvm, 63, "%s.fs.ll", prefix);
   snprintf(file_fs_llvm_opt, 63, "%s.fs.opt.ll", prefix);
   snprintf(file_vs_object, 63, "%s.vs", prefix);
   snprintf(file_fs_object, 63, "%s.fs", prefix);

   FILE *fp_nir = fopen(file_nir, "w");
   FILE *fp_nir_opt = fopen(file_nir_opt, "w");
   FILE *fp_vs = fopen(file_vs_llvm, "w");
   FILE *fp_vs_opt = fopen(file_vs_llvm_opt, "w");
   FILE *fp_fs = fopen(file_fs_llvm, "w");
   FILE *fp_fs_opt = fopen(file_fs_llvm_opt, "w");

   if (fp_nir == NULL || fp_nir_opt == NULL || fp_vs == NULL ||
       fp_vs_opt == NULL || fp_fs == NULL || fp_fs_opt == NULL) {
      printf("Failed to open file\n");
      return -1;
   }

   fsa_print_linked_prog_nir(whole_program, fp_nir);
   nir_shader *vs =
      whole_program->_LinkedShaders[MESA_SHADER_VERTEX]->Program->nir;
   nir_shader *fs =
      whole_program->_LinkedShaders[MESA_SHADER_FRAGMENT]->Program->nir;

   /* Preprocess nir */
   fsa_preprocess_nir(vs);
   fsa_preprocess_nir(fs);

   /* Lowering and opt */
   fsa_compile_nir_opt(vs);
   fsa_compile_nir_opt(fs);
   fsa_print_linked_prog_nir(whole_program, fp_nir_opt);

   /* Compile to llvm ir for each shader */
   LLVMModuleRef vert_module = fsa_nir_to_llvm(vs, false);
   LLVMModuleRef frag_module = fsa_nir_to_llvm(fs, false);
   fsa_print_llvm_module(vert_module, fp_vs);
   fsa_print_llvm_module(frag_module, fp_fs);

   fsa_llvm_verify_optimize_module(vert_module, fp_vs_opt);
   fsa_llvm_verify_optimize_module(frag_module, fp_fs_opt);
   fsa_print_llvm_module(vert_module, fp_vs_opt);
   fsa_print_llvm_module(frag_module, fp_fs_opt);

   /* Compile to riscv asm for each shader */
   fsa_llvm_riscv_init();
   assemble_llvm_module(vert_module, file_vs_object);
   assemble_llvm_module(frag_module, file_fs_object);

   fclose(fp_nir);
   fclose(fp_nir_opt);
   fclose(fp_vs);
   fclose(fp_vs_opt);
   fclose(fp_fs);
   fclose(fp_fs_opt);
   fsa_compiler_cleanup(whole_program);
   return 0;
}