#ifndef FSA_UTIL_H
#define FSA_UTIL_H

#include <stdio.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>

int fsa_llvm_verify_module(LLVMModuleRef module);
int fsa_llvm_verify_optimize_module(LLVMModuleRef module, FILE *fp);
void fsa_print_llvm_module(LLVMModuleRef module, FILE *fp);
void fsa_llvm_host_init(void);
void fsa_llvm_riscv_init(void);

#endif