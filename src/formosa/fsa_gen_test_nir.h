#ifndef FSA_GEN_NIR_H
#define FSA_GEN_NIR_H

#include "compiler/nir/nir.h"

struct fsa_nir_microbench_struct {
   const char *name;
   const char *desc;
   nir_shader *(*gen_nir)(const nir_shader_compiler_options *);
   int op_type;
   int num_ele;
   void (*gen_golden)(void *dest, void *a, void *b, void *c);
};
extern struct fsa_nir_microbench_struct fsa_nir_benchlist[];

uint32_t gen_valid_float(void);

#endif