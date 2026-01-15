#include "fsa_compiler.h"
#include "compiler/glsl/standalone.h"
#include "compiler/nir/nir.h"
#include "main/mtypes.h"
#include "main/shader_types.h"
#include <llvm-c/Analysis.h>
#include "fsa_llvm_build.h"
#include "fsa_llvm_verify.h"
#include "fsa_nir_to_llvm.h"
#include "fsa_util.h"

/** @file fsa_compiler.cc
 *
 * Wrapper for standalone compiler lib. Allow future development and study
 * of glsl compiler.
 */

struct standalone_options *
fsa_get_standalone_options(const struct fsa_options *f)
{
   return (struct standalone_options *)f;
}

struct gl_shader_program *
fsa_compile_shader(const struct fsa_options *_options, unsigned num_files,
                   char *const *files, struct gl_context *ctx)
{

   struct standalone_options *s_options = fsa_get_standalone_options(_options);
   struct gl_shader_program *prog =
      standalone_compile_shader(s_options, num_files, files, ctx);

   return prog;
}

void
fsa_compiler_cleanup(struct gl_shader_program *prog)
{
   standalone_compiler_cleanup(prog);
}

static void
fsa_print_nir_info(struct nir_shader *nir)
{
   printf("NIR shader info:\n");
   printf("\tNum inputs: %d\n", nir->num_inputs);
   printf("\tNum uniforms: %d\n", nir->num_uniforms);
   printf("\tNum outputs: %d\n", nir->num_outputs);
   printf("\tGlobal mem size: %d\n", nir->global_mem_size);
   printf("\tScratch size: %d\n", nir->scratch_size);
   printf("\tConstant data size: %d\n", nir->constant_data_size);
   printf("\n");
}

static void
fsa_print_linked_prog_nir(struct gl_shader_program *whole_prog,
                          const struct fsa_options *options)
{
   if (!options->base_option.do_link) {
      printf("Printing nir requires shaders to be linked first\n");
      return;
   }
   struct gl_program *vert_prog =
      whole_prog->_LinkedShaders[MESA_SHADER_VERTEX]->Program;
   struct gl_program *frag_prog =
      whole_prog->_LinkedShaders[MESA_SHADER_FRAGMENT]->Program;
   nir_print_shader(vert_prog->nir, stdout);
   fsa_print_nir_info(vert_prog->nir);
   nir_print_shader(frag_prog->nir, stdout);
   fsa_print_nir_info(frag_prog->nir);
}

LLVMModuleRef
fsa_nir_to_llvm(nir_shader *shader, FILE *fp, int is_cpu)
{
   if (fp)
      nir_print_shader(shader, fp);
   struct fsa_llvm_context tmp;
   fsa_llvm_context_init(&tmp);
   tmp.is_cpu = is_cpu;
   if (!fsa_nir_translate(&tmp, shader))
      return NULL;
   /* Insert a temp ret to pass verification */
   LLVMBuildRetVoid(tmp.builder);

   /* Dump */
   fsa_print_llvm_module(tmp.module, fp);
   if (fsa_llvm_verify_module(tmp.module)) {
      return NULL;
   }
   // if (fsa_llvm_verify_optimize_module(tmp.module)) {
   //    return NULL;
   // }
   fsa_print_llvm_module(tmp.module, fp);
   return tmp.module;
}

int
cmd_compile(int argc, const char **argv)
{
   struct gl_context ctx = {0};
   struct fsa_options options = {0};
   options.base_option.do_link = 1;
   options.base_option.glsl_version = 300;
   options.dump_nir = 1;
   if (argc < 3) {
      printf("Specify vert and frag files\n");
      return -1;
   }
   struct gl_shader_program *whole_program =
      fsa_compile_shader(&options, 2, (char *const *)&argv[1], &ctx);

   fsa_print_linked_prog_nir(whole_program, &options);
   fsa_compiler_cleanup(whole_program);
   return 0;
}