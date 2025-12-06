#ifndef FSA_GEN_NIR_H
#define FSA_GEN_NIR_H

#include "compiler/nir/nir.h"

struct fsa_nir_microbench_struct {
   const char *name;
   const char *desc;
   nir_shader *(*fn)(const nir_shader_compiler_options *);
};
extern struct fsa_nir_microbench_struct fsa_nir_benchlist[];

#endif