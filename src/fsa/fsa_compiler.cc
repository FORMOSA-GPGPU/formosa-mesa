#include "fsa_compiler.h"
#include "compiler/glsl/standalone.h"

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
   /* Always compile for gles3 */
   s_options->glsl_version = 320;
   gl_shader_program *prog =
      standalone_compile_shader(s_options, num_files, files, ctx);

   return prog;
}

extern "C" void
fsa_compiler_cleanup(gl_shader_program *prog)
{
   standalone_compiler_cleanup(prog);
}
