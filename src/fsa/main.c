/**
 * @file main.c
 * This is the entry of fsa glsl compiler used for experimenting glsl shader
 * compilation with mesa ir, nir, llvm ir.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "argparse.h"
#include "fsa_bench.h"

static const char *const usages[] = {
   "fsa_compiler bench",
   NULL,
};

struct cmd_struct {
   const char *cmd;
   int (*fn)(int, const char **);
};

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

   argc = argparse_parse(&argparse, argc, argv);

   if (argc < 1) {
      argparse_usage(&argparse);
      return -1;
   }

   const char *subcommand = argv[0];

   if (strcmp(subcommand, "bench") == 0) {
      return cmd_bench(argc, argv);
   } else {
      fprintf(stderr, "Unknown subcommand: %s\n", subcommand);
      return -1;
   }

   return 0;
}
