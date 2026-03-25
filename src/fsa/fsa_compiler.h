#ifndef FSA_COMPILER_H
#define FSA_COMPILER_H

#include "compiler/glsl/standalone.h"
#include "compiler/nir/nir.h"
#include "main/shader_types.h"
#include <llvm-c/Core.h>

#ifdef __cplusplus
extern "C" {
#endif

struct fsa_options {
   struct standalone_options base_option;
   int dump_nir;
   int compile;
};

int cmd_compile(int argc, const char **argv);
struct standalone_options *
fsa_get_standalone_options(const struct fsa_options *f);
// struct gl_shader_program *fsa_compile_shader(const struct fsa_options
// *_options,
//                                              unsigned num_files,
//                                              char *const *files,
//                                              struct gl_context *ctx);
void fsa_compiler_cleanup(struct gl_shader_program *prog);

bool fsa_preprocess_nir(nir_shader *nir);
bool fsa_compile_nir_opt(nir_shader *shader);
LLVMModuleRef fsa_nir_to_llvm(nir_shader *shader, int is_cpu);

extern const nir_shader_compiler_options fsa_nir_option;

bool fsa_assemble_llvm_module_to_mem(LLVMModuleRef module, void **out_buf,
                                     size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif
