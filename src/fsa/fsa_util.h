#ifndef FSA_UTIL_H
#define FSA_UTIL_H

#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>

int fsa_llvm_verify_module(LLVMModuleRef module);
#endif