/**
 * @file main.cc
 * This is the entry of fsa glsl compiler used for experimenting glsl shader
 * compilation with mesa ir, nir, llvm ir.
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include "main/mtypes.h"
#include "fsa_compiler.h"

static fsa_options options;
const struct option compiler_opts[] = {
   {"dump-ast", no_argument, &options.base_option.dump_ast, 1},
   {"dump-hir", no_argument, &options.base_option.dump_hir, 1},
   {"dump-lir", no_argument, &options.base_option.dump_lir, 1},
   {"link", no_argument, &options.base_option.do_link, 1},
   {"just-log", no_argument, &options.base_option.just_log, 1},
   {"lower-precision", no_argument, &options.base_option.lower_precision, 1},
   {"dump-nir", no_argument, &options.dump_nir, 1},
   {NULL, 0, NULL, 0}};

static void
print_usage(const char *name)
{
   const char *header = "usage: %s [options] <file.vert | file.frag>\n"
                        "\n"
                        "Possible options are:\n";
   printf(header, name);
   for (const struct option *o = compiler_opts; o->name != 0; ++o) {
      printf("    --%s", o->name);
      if (o->has_arg == required_argument)
         printf(" (mandatory)");
      printf("\n");
   }
}

/**
 * \brief Print proper usage and exit with failure.
 */
static void
usage_fail(const char *name)
{
   print_usage(name);
   exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
   int status = EXIT_SUCCESS;
   int c;
   int idx = 0;
   while ((c = getopt_long(argc, argv, "h", compiler_opts, &idx)) != -1) {
      switch (c) {
      case 'h':
         print_usage(argv[0]);
         exit(EXIT_SUCCESS);
      default:
         break;
      }
   }

   if (argc <= optind)
      usage_fail(argv[0]);

   struct gl_shader_program *whole_program;
   static struct gl_context local_ctx;

   whole_program =
      fsa_compile_shader(&options, argc - optind, &argv[optind], &local_ctx);

   fsa_compiler_cleanup(whole_program);

   return status;
}
