#include "fsa_bench.h"
#include <stdio.h>
#include <stdlib.h>
#include "argparse.h"
#include "fsa_compiler.h"
#include "fsa_gen_test_nir.h"
#include "fsa_llvm_verify.h"
#include "fsa_ubench_menu.h"

static int randseed = 0;

static const char *const bench_usages[] = {
   "fsa_compiler bench search",
   "fsa_compiler bench run",
   "fsa_compiler bench compile",
   NULL,
};

static const char *const run_usages[] = {
   "fsa_compiler run all",
   "fsa_compiler run [num]",
   NULL,
};

static const char *const compile_usages[] = {
   "fsa_compiler compile [num]",
   NULL,
};

static const char *const search_usages[] = {
   "fsa_compiler search [num]",
   NULL,
};

static int
parse_num(const char *str)
{
   char *endptr;
   int bench_index = strtol(str, &endptr, 10);
   if (*endptr != '\0' || bench_index < 0) {
      fprintf(stderr, "Error: Argument must be a valid number.\n");
      return -1;
   }
   return bench_index;
}

static LLVMModuleRef
fsa_bench_compile_test(FILE *fp, int index)
{
   /* We dont deal with pack/unpack ops that produces vectorized insts */
   const nir_shader_compiler_options option = {
      .lower_pack_half_2x16 = true,
      .lower_pack_unorm_2x16 = true,
      .lower_pack_snorm_2x16 = true,
      .lower_pack_unorm_4x8 = true,
      .lower_pack_snorm_4x8 = true,
      .lower_pack_64_2x32 = true,
      .lower_pack_64_4x16 = true,
      .lower_pack_32_2x16 = true,
      .lower_pack_64_2x32_split = true,
      .lower_pack_32_2x16_split = true,
      .lower_unpack_half_2x16 = true,
      .lower_unpack_unorm_2x16 = true,
      .lower_unpack_snorm_2x16 = true,
      .lower_unpack_unorm_4x8 = true,
      .lower_unpack_snorm_4x8 = true,
      .lower_unpack_64_2x32_split = true,
      .lower_unpack_32_2x16_split = true,
   };
   nir_shader *shader = fsa_nir_benchlist[index].gen_nir(&option);
   nir_lower_io_to_scalar(shader, nir_var_shader_in | nir_var_shader_out, NULL,
                          NULL);
   nir_lower_alu_to_scalar(shader, NULL, NULL);
   nir_lower_load_const_to_scalar(shader);
   return fsa_standalone_nir_to_llvm(shader, fp);
}

static int
bench_compile(int argc, const char **argv)
{
   struct argparse_option options[] = {
      OPT_HELP(),
      OPT_END(),
   };
   struct argparse argparse;
   argparse_init(&argparse, options, compile_usages, 0);
   argparse_describe(&argparse, "\nCompile a benchmark", "");

   argc = argparse_parse(&argparse, argc, argv);

   if (argc < 1) {
      printf("Require a index");
      return -1;
   }
   int i = parse_num(argv[0]);
   fsa_bench_compile_test(stdout, i);
   return 0;
}

static int
bench_run_index(FILE *fp, int i)
{
   LLVMModuleRef mod = fsa_bench_compile_test(fp, i);
   return fsa_llvm_verify_host(mod, fsa_nir_benchlist[i].op_type,
                               fsa_nir_benchlist[i].num_ele,
                               fsa_nir_benchlist[i].gen_golden);
}

static int
bench_run_all(FILE *fp)
{
   fsa_llvm_verify_host_init();
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
   int bench_index = parse_num(argv[0]);
   if (bench_index < 0) {
      return -1;
   }

   printf("Benchmark %d :\n%s | %s\n", bench_index,
          fsa_nir_benchlist[bench_index].name,
          fsa_nir_benchlist[bench_index].desc);
   return 0;
}

static int
bench_seed_cb(struct argparse *argparse, const struct argparse_option *opt)
{
   printf("Set random seed to %d\n", randseed);
   srand(randseed);
   return 0;
}

static int
bench_run(int argc, const char **argv)
{
   struct argparse_option options[] = {
      OPT_INTEGER('s', "seed", &randseed, "Random seed", bench_seed_cb, 0, 1),
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

   if (bench_index < 0) {
      return -1;
   }
   fsa_llvm_verify_host_init();
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
   argparse_init(&argparse, options, bench_usages, ARGPARSE_STOP_AT_NON_OPTION);
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
   } else if (!strncmp(argv[0], "compile", 7)) {
      return bench_compile(argc, argv);
   }
   argparse_usage(&argparse);
   return -1;
}