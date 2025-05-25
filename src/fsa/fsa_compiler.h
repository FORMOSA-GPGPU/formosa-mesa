#ifndef FSA_COMPILER_H
#define FSA_COMPILER_H

#include "compiler/glsl/standalone.h"
#include "main/shader_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct fsa_options {
   struct standalone_options base_option;
   int dump_nir;
};

struct standalone_options *
fsa_get_standalone_options(const struct fsa_options *f);
struct gl_shader_program *fsa_compile_shader(const struct fsa_options *_options,
                                             unsigned num_files,
                                             char *const *files,
                                             struct gl_context *ctx);
void fsa_compiler_cleanup(gl_shader_program *prog);

#ifdef __cplusplus
}
#endif

#endif
