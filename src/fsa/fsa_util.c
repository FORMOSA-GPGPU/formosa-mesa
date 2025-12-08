#include "fsa_util.h"
#include <stdio.h>
#include <llvm-c/Analysis.h>

int
fsa_llvm_verify_module(LLVMModuleRef module)
{
   char *error = NULL;
   if (LLVMVerifyModule(module, LLVMAbortProcessAction, &error)) {
      printf("Failed to verify LLVM module: %s\n", error);
      LLVMDisposeMessage(error);
      return -1;
   }
   return 0;
}