#include "fsa_llvm_verify.h"
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "uscsim/cbind.h"
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
#include "fsa_libm_gpu_bc.h"
#include "fsa_lld_helper.h"
#include "fsa_spike.S.h"
#include "fsa_spike.ld.h"
#include "fsa_spike_helper.h"
#include "fsa_uscsim.S.h"
#include "fsa_uscsim.ld.h"
#include "fsa_util.h"

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

static bool
is_nan(uint32_t val)
{
   return (val & 0x7f800000) == 0x7f800000 && (val & 0x007fffff) != 0;
}

static int
verify_answer(uint32_t *output, uint32_t *golden, uint32_t *input, int num_ele)
{
   for (int i = 0; i < num_ele; i++) {
      if (is_nan(golden[i]) && is_nan(output[i])) {
         continue;
      }

      // Allow for a small error margin for floating-point comparisons
      if (output[i] == golden[i])
         continue;

      int32_t diff = (int32_t)output[i] - (int32_t)golden[i];
      if (diff >= -1 && diff <= 1) {
         continue;
      } else {
         printf("\033[1;31mError [%d/%d]\033[0m: 0x%x != 0x%x\n", i + 1,
                num_ele, output[i], golden[i]);
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
assemble_and_link(LLVMModuleRef module, const char *triple, const char *cpu,
                  const char *features, const char *prefix,
                  const char *ld_content)
{
   char *error = NULL;
   char ld_name[64] = {0};
   char obj_name[64] = {0};
   char elf_name[64] = {0};
   char ir_name[64] = {0};
   snprintf(ld_name, 64, "/tmp/%s.ld", prefix);
   snprintf(obj_name, 64, "/tmp/%s.o", prefix);
   snprintf(elf_name, 64, "/tmp/%s.elf", prefix);
   snprintf(ir_name, 64, "/tmp/%s.ll", prefix);

   /* Assemble */
   LLVMTargetRef target;
   if (LLVMGetTargetFromTriple(triple, &target, &error) != 0) {
      printf("Error getting target: %s\n", error);
      return NULL;
   }
   LLVMTargetMachineRef target_machine = LLVMCreateTargetMachine(
      target, triple, cpu, features, LLVMCodeGenLevelAggressive,
      LLVMRelocStatic, LLVMCodeModelMedium);

   if (LLVMPrintModuleToFile(module, ir_name, &error) != 0) {
      printf("Error dumping IR: %s\n", error);
      return NULL;
   }

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
link_fsa_libm(LLVMModuleRef module, int simt)
{
   LLVMMemoryBufferRef libm_mem_buf = NULL;
   if (simt) {
      libm_mem_buf = LLVMCreateMemoryBufferWithMemoryRange(
         fsa_libm_gpu_bc, sizeof(fsa_libm_gpu_bc), "fsa_libm_gpu_bc", 0);
   } else {
      libm_mem_buf = LLVMCreateMemoryBufferWithMemoryRange(
         fsa_libm_bc, sizeof(fsa_libm_bc), "fsa_libm_bc", 0);
   }
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
   LLVMAppendModuleInlineAsm(module, fsa_spike_startup,
                             strlen(fsa_spike_startup));
   if (link_fsa_libm(module, false)) {
      return -1;
   }
   /* Final elf file name is returned */
   char *elf_name =
      assemble_and_link(module, "riscv64-unknown-elf", "generic-rv64",
                        "+m,+f,+d,+zicsr", "fsa_rvvm", fsa_spike_ld);
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

/*
   input layout (always exist a vec4 stride between input):
   | v0x | v0y | v0z | v0w | v1x | v1y | v1z | v1w | ...
   scalar-unary:
   | t0v0x | ????? | ????? | ????? | t1v0x | ????? | ????? | ????? | ...
   scalar-binary:
   | t0v0x | ????? | ????? | ????? | t0v1x | ????? | ????? | ????? | t1v0x | ...
   vec2-unary:
   | t0v0x | t0v1x | ????? | ????? | t1v0x | t1v1x | ????? | ????? | t2v0x | ...
*/
static void
prepare_data_simt(uint32_t *output, uint32_t *input, uint32_t *golden,
                  int op_type, int num_ele,
                  void (*fn_golden)(void *, void *, void *, void *))
{
   /* The hardcoded number "4" is the number of threads per warp */
   for (int t = 0; t < 4; t++) {
      for (int op = 0; op < op_type; op++) {
         for (int ele = 0; ele < num_ele; ele++) {
            input[t * op_type * 4 + op * 4 + ele] = gen_valid_float();
         }
      }
   }

   for (int t = 0; t < 4; t++) {
      for (int ele = 0; ele < num_ele; ele++) {
         /* clang-format off */
         fn_golden(&golden[t * 4 + ele],
                   &input[t * 4 * op_type + ele],
                   &input[t * 4 * op_type + ele + 4],
                   &input[t * 4 * op_type + ele + 8]);
         /* clang-format on */
      }
   }
   return;
}

/* Helper struct def */
struct GfxLayout {
   float *base;
   uint64_t stride;
};

struct GfxState {
   /* Vertex shader input */
   struct GfxLayout vert_attrib_generic[16];
   /* Vertex shader output, Fragment shader input */
   struct GfxLayout varying_slot_pos[1];
   struct GfxLayout varying_slot_tex[8];
   struct GfxLayout varying_slot_var[32];
   struct GfxLayout frag_result_data[8];
   /* Visibility stream */
   uint64_t *VisS_base;
};

struct GfxVSInfo {
   struct GfxState *gfx_state;
   uint32_t wg_id;
   uint32_t wg_size;
};

/*
MEMORY MAP (High -> Low)
========================

          0x10000000
          |
    +--------------------------------------+
    |                global                |  <-- 128.0 MB
    +--------------------------------------+
          |
          0x08000000
          |
          0x07FFFFFF
          |
    +--------------------------------------+
    |               program                |  <-- 128.0 MB
    +--------------------------------------+
          |
          0x00000000

========================
Info, state, and input/output will be put into global space
info: 0x8000000
state: 0x8000100
input: 0x8002000
pos(output): 0x8003000
*/
static void
gfx_info_init(struct GfxVSInfo *info)
{
   info->gfx_state = (struct GfxState *)0x8000100;
   info->wg_id = 0;
   info->wg_size = 4;
}

static void
gfx_state_init(struct GfxState *s, int op_type)
{
   /* uint32_t input[3 * 4 * 4] */
   /* Only use Generic0-2 as input */
   uint64_t input_base = 0x8002000;
   uint64_t output_base = 0x8003000;
   s->vert_attrib_generic[0].base = (float *)input_base;
   s->vert_attrib_generic[0].stride = 4 * 4 * op_type;
   s->vert_attrib_generic[1].base = (float *)(input_base + 4 * 4);
   s->vert_attrib_generic[1].stride = 4 * 4 * op_type;
   s->vert_attrib_generic[2].base = (float *)(input_base + 4 * 4 * 2);
   s->vert_attrib_generic[2].stride = 4 * 4 * op_type;
   for (int i = 3; i < 16; i++) {
      s->vert_attrib_generic[i].base = (float *)0xdeadbeef;
      s->vert_attrib_generic[i].stride = 0xdeadbeef;
   }
   /* Pos */
   s->varying_slot_pos[0].base = (float *)output_base;
   s->varying_slot_pos[0].stride = 4 * 4;
   /* Tex */
   for (int i = 0; i < 8; i++) {
      s->varying_slot_tex[i].base = (float *)0xdeadbeef;
      s->varying_slot_tex[i].stride = 0xdeadbeef;
   }
   /* Var */
   for (int i = 0; i < 32; i++) {
      s->varying_slot_var[i].base = (float *)0xdeadbeef;
      s->varying_slot_var[i].stride = 0xdeadbeef;
   }
   /* Fres */
   for (int i = 0; i < 8; i++) {
      s->frag_result_data[i].base = (float *)0xdeadbeef;
      s->frag_result_data[i].stride = 0xdeadbeef;
   }
   return;
}

int
fsa_llvm_verify_uscsim(LLVMModuleRef module, int op_type, int num_ele,
                       void (*fn_golden)(void *, void *, void *, void *))
{
   LLVMAppendModuleInlineAsm(module, fsa_uscsim_startup,
                             strlen(fsa_uscsim_startup));
   if (link_fsa_libm(module, true)) {
      return -1;
   }
   char *elf_name =
      assemble_and_link(module, "riscv64-unknown-elf", "formosa-gpgpu", "-f,-d",
                        "fsa_uscsim", fsa_uscsim_ld);
   if (!elf_name) {
      return -1;
   }

   USCSimSystem *usc = uscsim_create(0x10000000, false, false);
   USCSimKernel *kernel = uscsim_create_kernel(elf_name);
   /* clang-format off */
   /*
      Doing SIMT verication with 1 warp / 4 threads
      input layout (always exist a vec4 stride between input):
      | v0x | v0y | v0z | v0w | v1x | v1y | v1z | v1w | ...
      scalar-unary:
      | t0v0x | ????? | ????? | ????? | t1v0x | ????? | ????? | ????? | ...
      scalar-binary:
      | t0v0x | ????? | ????? | ????? | t0v1x | ????? | ????? | ????? | t1v0x | ...
      vec2-unary:
      | t0v0x | t0v1x | ????? | ????? | t1v0x | t1v1x | ????? | ????? | t2v0x | ...
   */
   /* clang-format on */

   uint32_t input[3 * 4 * 4] = {0};
   uint32_t output[4 * 4] = {0};
   uint32_t golden[4 * 4] = {0};

   prepare_data_simt(output, input, golden, op_type, num_ele, fn_golden);

   /* Set up required info and state */
   struct GfxVSInfo vsinfo = {0};
   struct GfxState gfxstate = {0};
   gfx_info_init(&vsinfo);
   gfx_state_init(&gfxstate, op_type);
   uscsim_write_mem(usc, 0x8000000, sizeof(struct GfxVSInfo),
                    (uint8_t *)&vsinfo);
   uscsim_write_mem(usc, 0x8000100, sizeof(struct GfxState),
                    (uint8_t *)&gfxstate);
   uscsim_write_mem(usc, 0x8002000, 3 * 4 * 4 * sizeof(uint32_t),
                    (uint8_t *)input);

   /* Setup uscsim and launch kernel */
   uscsim_set_pointers(usc, 0x8000000, 0, 0x8000100);

   uscsim_launch_vs(usc, kernel, 4);

   uscsim_read_mem(usc, (uint8_t *)output, 0x8003000, 4 * 4 * sizeof(uint32_t));
   /* Verify result */
   int res = verify_answer(output, golden, input, 16);

   uscsim_kernel_destroy(kernel);
   uscsim_destroy(usc);
   free(elf_name);
   return res;
}