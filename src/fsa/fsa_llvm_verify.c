#include "fsa_llvm_verify.h"
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitReader.h>
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Linker.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include "fsa_gen_test_nir.h"
#include "fsa_libm_bc.h"
#include "fsa_lld_helper.h"
#include "fsa_spike_helper.h"
#include "fsa_util.h"
#include "fsa_spike.ld.h"
#include "fsa_spike.S.h"

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

void
fsa_llvm_verify_riscv_init()
{
   LLVMInitializeRISCVTargetInfo();
   LLVMInitializeRISCVTarget();
   LLVMInitializeRISCVTargetMC();
   LLVMInitializeRISCVAsmPrinter();
   LLVMInitializeRISCVAsmParser();
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
   LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(function);
   /* if/else test may contain more than 1 basic block, but there will be only
    * one valid load/store in each test. Therefore, reset load/store count every
    * block */
   while (bb) {
      LLVMValueRef inst = LLVMGetFirstInstruction(bb);
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
      bb = LLVMGetNextBasicBlock(bb);
   }
}

static void
prepare_data(uint32_t *output, uint32_t *input, uint32_t *golden, int op_type,
             int num_ele, void (*fn_golden)(void *, void *, void *, void *))
{
   for (int i = 0; i < op_type; i++) {
      for (int j = 0; j < num_ele; j++) {
         input[i * num_ele + j] = gen_valid_float();
      }
   }

   for (int i = 0; i < num_ele; i++) {
      fn_golden(&golden[i], &input[i], &input[i + num_ele * 1],
                &input[i + num_ele * 2]);
   }
}

static int
verify_answer(uint32_t *output, uint32_t *golden, uint32_t *input, int num_ele)
{
   for (int i = 0; i < num_ele; i++) {
      /* If is NaN, dont care signedness */
      if (golden[i] == 0xffc00000 || golden[i] == 0x7fc00000) {
         if (output[i] != 0xffc00000 && output[i] != 0x7fc00000) {
            printf(
               "\033[1;31mError [%d/%d]\033[0m: 0x%x 0x%x 0x%x = 0x%x != 0x%x\n",
               i + 1, num_ele, input[i], input[i + num_ele],
               input[i + num_ele * 2], output[i], golden[i]);
            return -1;
         }
         continue;
      }

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

   prepare_data(output, input, golden, op_type, num_ele, fn_golden);

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

   return verify_answer(output, golden, input, num_ele);
}

static int
write_to_file(const char *filename, const char *start)
{
   FILE *f = fopen(filename, "w");
   if (!f)
      return 1;
   fwrite(start, sizeof(char), strlen(start), f);
   fclose(f);
   return 0;
}

static char *
read_from_file(const char *filename, size_t *size)
{
   FILE *f = fopen(filename, "rb");
   if (!f) {
      perror("Failed to open file");
      return NULL;
   }
   fseek(f, 0, SEEK_END);
   size_t length = ftell(f);
   if (length < 0) {
      fclose(f);
      return NULL;
   }

   fseek(f, 0, SEEK_SET);
   char *buffer = malloc(length + 1);
   if (!buffer) {
      perror("Failed to allocate memory");
      fclose(f);
      return NULL;
   }
   size_t read_size = fread(buffer, 1, length, f);
   if (read_size != length) {
      free(buffer);
      fclose(f);
      return NULL;
   }
   buffer[length] = '\0';
   fclose(f);

   *size = length;
   return buffer;
}

static char *
assemble_and_link(LLVMModuleRef module, const char *triple, const char *prefix,
                  const char *ld_content)
{
   char *error = NULL;
   char ld_name[64] = {0};
   char obj_name[64] = {0};
   char elf_name[64] = {0};
   snprintf(ld_name, 64, "%s.ld", prefix);
   snprintf(obj_name, 64, "%s.o", prefix);
   snprintf(elf_name, 64, "%s.elf", prefix);

   /* Assemble */
   LLVMTargetRef target;
   if (LLVMGetTargetFromTriple(triple, &target, &error) != 0) {
      printf("Error getting target: %s\n", error);
      return NULL;
   }
   const char *cpu = "generic-rv64";
   const char *features = "+m,+f,+d,+zicsr";
   LLVMTargetMachineRef target_machine = LLVMCreateTargetMachine(
      target, triple, cpu, features, LLVMCodeGenLevelAggressive,
      LLVMRelocStatic, LLVMCodeModelMedium);
   if (LLVMTargetMachineEmitToFile(target_machine, module, obj_name,
                                   LLVMObjectFile, &error) != 0) {
      printf("Error emitting to file :%s\n", error);
      return NULL;
   }

   /* Write linker script */
   if (write_to_file(ld_name, ld_content)) {
      return NULL;
   }

   /* Link */
   char ldfile_flag[64] = {0};
   char output_flag[64] = {0};
   snprintf(ldfile_flag, 64, "-T%s", ld_name);
   snprintf(output_flag, 64, "-o%s", elf_name);
   char *lld_args[] = {"-melf64lriscv", "--gc-sections", ldfile_flag, obj_name,
                       output_flag};
   bool ld_res = fsa_lld_link_elf(sizeof(lld_args) / sizeof(lld_args[0]),
                                  (const char **)lld_args, &error);
   if (!ld_res) {
      printf("Linker failed with:%s\n", error);
      fsa_lld_free_msg(error);
      return NULL;
   }

   LLVMDisposeTargetMachine(target_machine);
   return strdup(elf_name);
}

static int
link_fsa_libm(LLVMModuleRef module)
{
   LLVMMemoryBufferRef libm_mem_buf = LLVMCreateMemoryBufferWithMemoryRange(
      fsa_libm_bc, sizeof(fsa_libm_bc), "fsa_libm_bc", 0);
   LLVMModuleRef libm_mod = NULL;
   if (LLVMParseBitcode2(libm_mem_buf, &libm_mod) != 0) {
      printf("Error parsing libm bitcode\n");
      return -1;
   }

   if (LLVMLinkModules2(module, libm_mod) != 0) {
      printf("Error linking libm\n");
      return -1;
   }
   return 0;
}

static void
patch_spike_load_store(LLVMModuleRef module, LLVMTypeRef ele_type)
{
   /* Same logic as `patch_host` but the address were hardcoded */
   const uintptr_t input_addr_base = 0x300000;
   const uintptr_t output_addr_base = 0x350000;
   LLVMValueRef function = LLVMGetFirstFunction(module);
   LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(function);
   while (bb) {
      LLVMValueRef inst = LLVMGetFirstInstruction(bb);
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
      bb = LLVMGetNextBasicBlock(bb);
   }
}

int
fsa_llvm_verify_spike(LLVMModuleRef module, int op_type, int num_ele,
                      void (*fn_golden)(void *, void *, void *, void *))
{
   /* Preprocess ir module */
   patch_spike_load_store(module, LLVMFloatType());
   LLVMAppendModuleInlineAsm(module, fsa_spike_startup, strlen(fsa_spike_startup));
   if (link_fsa_libm(module)) {
      return -1;
   }
   /* Final elf file name is returned */
   char *elf_name =
      assemble_and_link(module, "riscv64-unknown-elf", "fsa_rvvm", fsa_spike_ld);
   if (!elf_name) {
      return -1;
   }

   SpikeMachine *vm = spike_create(elf_name, 0x400000);
   if (!vm) {
      printf("Error creating rvvm machine\n");
      return -1;
   }

   uint32_t input[3 * 4] = {0};
   uint32_t output[4] = {0};
   uint32_t golden[4] = {0};
   prepare_data(output, input, golden, op_type, num_ele, fn_golden);

   spike_write_memory(vm, 0x300000, input, sizeof(input));
   spike_run(vm);
   spike_read_memory(vm, output, 0x350000, sizeof(output));

   spike_destroy(vm);
   free(elf_name);

   return verify_answer(output, golden, input, num_ele);
}