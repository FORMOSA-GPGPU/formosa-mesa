#ifndef FSA_LLVM_VERIFY_H
#define FSA_LLVM_VERIFY_H

#include <llvm-c/Core.h>

void fsa_llvm_verify_host_init(void);
int fsa_llvm_verify_optimize_module(LLVMModuleRef module);
int fsa_llvm_verify_host(LLVMModuleRef module, int op_type, int num_ele,
                         void (*fn_golden)(void *, void *, void *, void *));

#endif