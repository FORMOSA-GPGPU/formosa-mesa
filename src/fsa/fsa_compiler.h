#ifndef FSA_COMPILER_H
#define FSA_COMPILER_H

#include "compiler/glsl/standalone.h"
#include "compiler/nir/nir.h"
#include "main/shader_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct fsa_options {
   struct standalone_options base_option;
   int dump_nir;
   int compile;
};

struct standalone_options *
fsa_get_standalone_options(const struct fsa_options *f);
struct gl_shader_program *fsa_compile_shader(const struct fsa_options *_options,
                                             unsigned num_files,
                                             char *const *files,
                                             struct gl_context *ctx);
void fsa_compiler_cleanup(struct gl_shader_program *prog);
void fsa_compiler_operation(struct gl_shader_program *whole_prog,
                            const struct fsa_options *options);

void fsa_standalone_nir_to_llvm(nir_shader *shader);

#ifdef __cplusplus
}
#endif

#endif
