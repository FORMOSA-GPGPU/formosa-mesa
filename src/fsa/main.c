/**
 * @file main.c
 * This is the entry of fsa glsl compiler used for experimenting glsl shader
 * compilation with mesa ir, nir, llvm ir.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "argparse.h"
#include "fsa_compiler.h"
#include "fsa_gen_test_nir.h"

static const char *const usages[] = {
   "fsa_compiler bench",
   "fsa_compiler bench [num]",
   NULL,
};

struct cmd_struct {
   const char *cmd;
   int (*fn)(int, const char **);
};

static int
cmd_bench(int argc, const char **argv)
{
   int bench_index = -1;

   struct argparse_option options[] = {
      OPT_HELP(),
      OPT_END(),
   };

   struct argparse argparse;
   argparse_init(&argparse, options, NULL, 0);
   argparse_describe(
      &argparse, "\nRun a specific benchmark.",
      "\nIf no index is provided, an interactive menu is shown.");

   argc = argparse_parse(&argparse, argc, argv);

   if (argc > 0) {
      char *endptr;
      bench_index = strtol(argv[0], &endptr, 10);
      if (*endptr != '\0') {
         fprintf(stderr, "Error: Argument must be a number.\n");
         return 1;
      }
   } else {
      bench_index = fsa_select_bench_interactive();
   }
   if (bench_index < 0 || bench_index >= fsa_bench_count()) {
      printf("Error benchmark index : %d\n", bench_index);
      exit(-1);
   }
   nir_shader_compiler_options option = {0};
   nir_shader *shader = fsa_nir_benchlist[bench_index].fn(&option);
   fsa_standalone_nir_to_llvm(shader);
   return 0;
}

int
main(int argc, const char **argv)
{
   struct argparse_option options[] = {
      OPT_HELP(),
      OPT_END(),
   };

   struct argparse argparse;
   argparse_init(&argparse, options, usages, ARGPARSE_STOP_AT_NON_OPTION);
   argparse_describe(&argparse, "fsa_compiler", NULL);

   int remaining_argc = argparse_parse(&argparse, argc, argv);

   if (remaining_argc < 1) {
      argparse_usage(&argparse);
      return -1;
   }

   const char *subcommand = argv[0];

   if (strcmp(subcommand, "bench") == 0) {
      return cmd_bench(remaining_argc, argv);
   } else {
      fprintf(stderr, "Unknown subcommand: %s\n", subcommand);
      return -1;
   }

   return 0;
}
