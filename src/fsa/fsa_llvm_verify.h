#ifndef FSA_LLVM_VERIFY_H
#define FSA_LLVM_VERIFY_H

#include <stdio.h>
#include <llvm-c/Core.h>

int fsa_llvm_verify_host(LLVMModuleRef module, int op_type, int num_ele,
                         void (*fn_golden)(void *, void *, void *, void *));
int fsa_llvm_verify_rvvm(LLVMModuleRef module, int op_type, int num_ele,
                         void (*fn_golden)(void *, void *, void *, void *));
int fsa_llvm_verify_spike(LLVMModuleRef module, int op_type, int num_ele,
                          void (*fn_golden)(void *, void *, void *, void *));
int fsa_llvm_verify_uscsim(LLVMModuleRef module, int op_type, int num_ele,
                           void (*fn_golden)(void *, void *, void *, void *));

#endif