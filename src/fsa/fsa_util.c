#include "fsa_util.h"
#include <stdio.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitReader.h>
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Linker.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Transforms/PassBuilder.h>

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

void
fsa_print_llvm_module(LLVMModuleRef module, FILE *fp)
{
   if (!fp)
      return;
   char *ir_str = LLVMPrintModuleToString(module);
   fprintf(fp, "%s", ir_str);
   LLVMDisposeMessage(ir_str);
}

void
fsa_llvm_host_init()
{
   LLVMInitializeNativeTarget();
   LLVMInitializeNativeAsmPrinter();
   LLVMInitializeNativeAsmParser();
   LLVMLinkInMCJIT();
}

void
fsa_llvm_riscv_init()
{
   LLVMInitializeRISCVTargetInfo();
   LLVMInitializeRISCVTarget();
   LLVMInitializeRISCVTargetMC();
   LLVMInitializeRISCVAsmPrinter();
   LLVMInitializeRISCVAsmParser();
}