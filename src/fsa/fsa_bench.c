#include "fsa_bench.h"
#include <stdio.h>
#include <stdlib.h>
#include "argparse.h"
#include "fsa_compiler.h"
#include "fsa_gen_test_nir.h"
#include "fsa_ubench_menu.h"

static const char *const bench_usages[] = {
   "fsa_compiler bench search",
   "fsa_compiler bench run",
   NULL,
};

static const char *const run_usages[] = {
   "fsa_compiler run all",
   "fsa_compiler run [num]",
   NULL,
};

static const char *const search_usages[] = {
   "fsa_compiler search [num]",
   NULL,
};

static int
bench_run_index(FILE *fp, int i)
{
   nir_shader_compiler_options option = {0};
   nir_shader *shader = fsa_nir_benchlist[i].fn(&option);
   return fsa_standalone_nir_to_llvm(shader, fp);
}

static int
bench_run_all(FILE *fp)
{
   int count = fsa_bench_count();
   for (int i = 0; i < count; i++) {
      printf("Running test (%d/%d)\n", i, count - 1);
      if (bench_run_index(NULL, i))
         return -1;
   }
   return 0;
}

static int
bench_search(int argc, const char **argv)
{
   struct argparse_option options[] = {
      OPT_HELP(),
      OPT_END(),
   };
   struct argparse argparse;
   argparse_init(&argparse, options, search_usages, 0);
   argparse_describe(&argparse, "\nSearch for a benchmark", "");

   argc = argparse_parse(&argparse, argc, argv);

   if (argc < 1) {
      printf("Require a index");
      return -1;
   }
   char *endptr;
   int bench_index = strtol(argv[0], &endptr, 10);
   if (*endptr != '\0') {
      fprintf(stderr, "Error: Argument must be a number.\n");
      return 1;
   }

   printf("Benchmark %d :\n%s | %s\n", bench_index,
          fsa_nir_benchlist[bench_index].name,
          fsa_nir_benchlist[bench_index].desc);
   return 0;
}

static int
bench_run(int argc, const char **argv)
{
   struct argparse_option options[] = {
      OPT_HELP(),
      OPT_END(),
   };
   struct argparse argparse;
   argparse_init(&argparse, options, run_usages, 0);
   argparse_describe(
      &argparse, "\nRun a specific benchmark.",
      "\nIf no index is provided, an interactive menu is shown.");

   argc = argparse_parse(&argparse, argc, argv);

   if (argc > 0 && !strncmp(argv[0], "all", 3)) {
      return bench_run_all(stdout);
   }

   int bench_index = -1;
   if (argc < 1) {
      bench_index = fsa_select_bench_interactive();
   } else {
      char *endptr;
      bench_index = strtol(argv[0], &endptr, 10);
      if (*endptr != '\0') {
         fprintf(stderr, "Error: Argument must be a number.\n");
         return 1;
      }
   }
   return bench_run_index(stdout, bench_index);
}

int
cmd_bench(int argc, const char **argv)
{
   struct argparse_option options[] = {
      OPT_HELP(),
      OPT_END(),
   };

   struct argparse argparse;
   argparse_init(&argparse, options, bench_usages, 0);
   argparse_describe(&argparse, "\nFSA compiler benchmark suite", "");

   argc = argparse_parse(&argparse, argc, argv);

   if (argc < 1) {
      argparse_usage(&argparse);
      return -1;
   }

   if (!strncmp(argv[0], "search", 6)) {
      return bench_search(argc, argv);
   } else if (!strncmp(argv[0], "run", 3)) {
      return bench_run(argc, argv);
   }
   return 0;
}