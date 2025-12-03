#ifndef FSA_GEN_NIR_H
#define FSA_GEN_NIR_H

#include "compiler/nir/nir.h"

struct fsa_nir_microbench_struct {
   const char *name;
   const char *desc;
   nir_shader *(*fn)(const nir_shader_compiler_options *);
};
extern struct fsa_nir_microbench_struct fsa_nir_benchlist[];

int fsa_bench_count(void);
int fsa_select_bench_interactive(void);

nir_shader *
fsa_gen_add_store_without_input(const nir_shader_compiler_options *options);

nir_shader *
fsa_gen_load_add_store_scalar(const nir_shader_compiler_options *options);

nir_shader *
fsa_gen_load_add_store_vec3(const nir_shader_compiler_options *options);

nir_shader *
fsa_gen_load_add_store_vec4(const nir_shader_compiler_options *options);
#endif