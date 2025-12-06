#include "fsa_compiler.h"
#include "compiler/glsl/standalone.h"
#include "compiler/nir/nir.h"
#include "main/shader_types.h"
#include <llvm-c/Analysis.h>
#include "fsa_llvm_build.h"
#include "fsa_nir_to_llvm.h"

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

static int
llvm_verify_and_report(LLVMModuleRef module)
{
   char *error = NULL;

   if (LLVMVerifyModule(module, LLVMReturnStatusAction, &error)) {
      fprintf(stderr, "LLVM Module verification failed:\n%s\n", error);
      LLVMDisposeMessage(error);
      LLVMDumpModule(module);
      return -1;
   }
   return 0;
}

int
fsa_standalone_nir_to_llvm(nir_shader *shader, FILE *fp)
{
   if (fp)
      nir_print_shader(shader, fp);
   struct fsa_llvm_context tmp;
   fsa_llvm_context_init(&tmp);
   if (!fsa_nir_translate(&tmp, shader))
      return -1;
   /* Insert a temp ret to pass verification */
   LLVMBuildRetVoid(tmp.builder);

   /* Dump */
   if (llvm_verify_and_report(tmp.module)) {
      return -1;
   }
   if (!fp)
      return 0;
   char *ir_str = LLVMPrintModuleToString(tmp.module);
   fprintf(fp,
           "###### DUMPING LLVM IR ######\n%s\n###########################\n",
           ir_str);
   LLVMDisposeMessage(ir_str);
   return 0;
}

void
fsa_compiler_operation(struct gl_shader_program *whole_prog,
                       const struct fsa_options *options)
{
   struct gl_program *vert_prog =
      whole_prog->_LinkedShaders[MESA_SHADER_VERTEX]->Program;
   struct gl_program *frag_prog =
      whole_prog->_LinkedShaders[MESA_SHADER_FRAGMENT]->Program;

   nir_lower_load_const_to_scalar(vert_prog->nir);
   nir_lower_alu_to_scalar(vert_prog->nir, NULL, NULL);

   if (options->dump_nir) {
      fsa_print_linked_prog_nir(whole_prog, options);
   }
}
