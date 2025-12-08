#include "fsa_llvm_verify.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include "fsa_gen_test_nir.h"
#include "fsa_nir_to_llvm.h"
#include "fsa_util.h"

int
fsa_llvm_verify_optimize_module(LLVMModuleRef module)
{
   LLVMTargetMachineRef target_machine = NULL;
   LLVMPassBuilderOptionsRef options = LLVMCreatePassBuilderOptions();

   /* Configure options */
   LLVMPassBuilderOptionsSetVerifyEach(options, 0);
   LLVMPassBuilderOptionsSetDebugLogging(options, 0);

   const char *passes = "default<O1>";
   LLVMErrorRef error = LLVMRunPasses(module, passes, target_machine, options);
   if (error) {
      char *err_msg = LLVMGetErrorMessage(error);
      fprintf(stderr, "Optimization failed: %s\n", err_msg);
      LLVMDisposeErrorMessage(err_msg);
      return -1;
   }

   LLVMDisposePassBuilderOptions(options);
   return 0;
}

void
fsa_llvm_verify_host_init()
{
   LLVMInitializeNativeTarget();
   LLVMInitializeNativeAsmPrinter();
   LLVMInitializeNativeAsmParser();
   LLVMLinkInMCJIT();
}

static LLVMValueRef
create_pointer(uintptr_t addr, LLVMTypeRef ele_type)
{
   return LLVMConstIntToPtr(LLVMConstInt(LLVMInt64Type(), addr, 0),
                            LLVMPointerType(ele_type, 0));
}

static size_t
get_type_size(LLVMModuleRef module, LLVMTypeRef type)
{
   LLVMTargetDataRef target_data = LLVMGetModuleDataLayout(module);
   size_t size_bytes = LLVMABISizeOfType(target_data, type);
   return size_bytes;
}

static void
patch_host_load_store(LLVMModuleRef module, uintptr_t input_addr_base,
                      uintptr_t output_addr_base, LLVMTypeRef ele_type)
{
   LLVMValueRef function = LLVMGetFirstFunction(module);
   assert("Should be only one block" && LLVMCountBasicBlocks(function) == 1);
   LLVMBasicBlockRef entry = LLVMGetFirstBasicBlock(function);
   LLVMValueRef inst = LLVMGetFirstInstruction(entry);
   int num_load = 0, num_store = 0;
   while (inst) {
      LLVMOpcode opcode = LLVMGetInstructionOpcode(inst);
      if (opcode == LLVMLoad) {
         LLVMValueRef addr = create_pointer(
            input_addr_base + get_type_size(module, ele_type) * num_load,
            ele_type);
         /* operand 0 is the address */
         LLVMSetOperand(inst, 0, addr);
         num_load++;
      } else if (opcode == LLVMStore) {
         LLVMValueRef addr = create_pointer(
            output_addr_base + get_type_size(module, ele_type) * num_store,
            ele_type);
         /* operand 1 is the address */
         LLVMSetOperand(inst, 1, addr);
         num_store++;
      }
      inst = LLVMGetNextInstruction(inst);
   }
}

int
fsa_llvm_verify_host(LLVMModuleRef module, int op_type, int num_ele,
                     void (*fn_golden)(void *, void *, void *, void *))
{
   /*
      Allocate enough size for every possible test size on the stack,
      max size = 3 operand * vec4
    */
   uint32_t input[3 * 4] = {0};
   uint32_t output[4] = {0};
   uint32_t golden[4] = {0};
   patch_host_load_store(module, (uintptr_t)input, (uintptr_t)output,
                         LLVMFloatType());
   fsa_llvm_verify_module(module);

   /* Prepare input and golden */
   for (int i = 0; i < op_type; i++) {
      for (int j = 0; j < num_ele; j++) {
         input[i * num_ele + j] = gen_valid_float();
      }
   }

   for (int i = 0; i < num_ele; i++) {
      fn_golden(&golden[i], &input[i], &input[i + num_ele * 1],
                &input[i + num_ele * 2]);
   }

   /* Set module target triple */
   char *triple = "x86_64-pc-linux-gnu";
   LLVMSetTarget(module, triple);

   /* Create MCJIT engine */
   struct LLVMMCJITCompilerOptions options;
   LLVMInitializeMCJITCompilerOptions(&options, sizeof(options));
   options.OptLevel = 2;
   LLVMExecutionEngineRef engine;
   char *error = NULL;
   if (LLVMCreateMCJITCompilerForModule(&engine, module, &options,
                                        sizeof(options), &error) != 0) {
      printf("Failed to create execution engine: %s\n", error);
      LLVMDisposeMessage(error);
      return -1;
   }
   LLVMValueRef function = LLVMGetFirstFunction(module);

   /* Start the function */
   LLVMRunFunction(engine, function, 0, NULL);

   /* Verify  */
   for (int i = 0; i < num_ele; i++) {
      if (output[i] != golden[i]) {
         printf(
            "\033[1;31mError [%d/%d]\033[0m: 0x%x 0x%x 0x%x = 0x%x != 0x%x\n",
            i + 1, num_ele, input[i], input[i + num_ele],
            input[i + num_ele * 2], output[i], golden[i]);
         return -1;
      }
   }

   return 0;
}