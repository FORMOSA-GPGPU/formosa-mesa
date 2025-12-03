#ifndef FSA_LLVM_BUILD_H
#define FSA_LLVM_BUILD_H

#include <stdbool.h>
#include <llvm-c/Core.h>

struct fsa_llvm_pointer {
   union {
      LLVMValueRef value;
      LLVMValueRef v;
   };
   union {
      LLVMTypeRef pointee_type;
      LLVMTypeRef t;
   };
};

struct fsa_llvm_context {
   LLVMContextRef context;
   LLVMModuleRef module;
   LLVMBuilderRef builder;

   struct fsa_llvm_pointer main_function;

   LLVMTypeRef voidt;
   LLVMTypeRef i1;
   LLVMTypeRef i8;
   LLVMTypeRef i16;
   LLVMTypeRef i32;
   LLVMTypeRef i64;
   LLVMTypeRef f16;
   LLVMTypeRef f32;
   LLVMTypeRef f64;
   LLVMTypeRef v2i32;
   LLVMTypeRef v3i32;
   LLVMTypeRef v4i32;
   LLVMTypeRef v2f32;
   LLVMTypeRef v3f32;
   LLVMTypeRef v4f32;

   LLVMValueRef i8_0;
   LLVMValueRef i8_1;
   LLVMValueRef i16_0;
   LLVMValueRef i16_1;
   LLVMValueRef i32_0;
   LLVMValueRef i32_1;
   LLVMValueRef i64_0;
   LLVMValueRef i64_1;
   LLVMValueRef i128_0;
   LLVMValueRef i128_1;
   LLVMValueRef f16_0;
   LLVMValueRef f16_1;
   LLVMValueRef f32_0;
   LLVMValueRef f32_1;
   LLVMValueRef f64_0;
   LLVMValueRef f64_1;
   LLVMValueRef i1true;
   LLVMValueRef i1false;
};

void fsa_print_llvm_type(const char *label, LLVMTypeRef t);

void fsa_llvm_context_init(struct fsa_llvm_context *ctx);
void fsa_llvm_context_dispose(struct fsa_llvm_context *ctx);

struct fsa_llvm_pointer fsa_build_main(struct fsa_llvm_context *ctx,
                                       const char *name, LLVMTypeRef ret_type,
                                       LLVMModuleRef module);

int fsa_get_llvm_num_components(LLVMValueRef value);

LLVMValueRef fsa_build_gather_values_extended(struct fsa_llvm_context *ctx,
                                              LLVMValueRef *values,
                                              unsigned value_count,
                                              unsigned value_stride,
                                              bool always_vector);
LLVMValueRef fsa_build_gather_values(struct fsa_llvm_context *ctx,
                                     LLVMValueRef *values,
                                     unsigned value_count);
LLVMTypeRef fsa_to_float_type(struct fsa_llvm_context *ctx, LLVMTypeRef t);
LLVMValueRef fsa_to_float(struct fsa_llvm_context *ctx, LLVMValueRef v);
LLVMValueRef fsa_build_intrinsic(struct fsa_llvm_context *ctx, const char *name,
                                 LLVMTypeRef return_type, LLVMValueRef *params,
                                 unsigned param_count, unsigned attrib_mask);
LLVMValueRef fsa_build_canonicalize(struct fsa_llvm_context *ctx,
                                    LLVMValueRef src0, unsigned bitsize);
LLVMValueRef fsa_to_integer(struct fsa_llvm_context *ctx, LLVMValueRef v);
LLVMValueRef fsa_to_integer_or_pointer(struct fsa_llvm_context *ctx,
                                       LLVMValueRef v);
LLVMTypeRef fsa_to_integer_type(struct fsa_llvm_context *ctx, LLVMTypeRef t);
int fsa_get_elem_bits(struct fsa_llvm_context *ctx, LLVMTypeRef type);
LLVMValueRef fsa_llvm_extract_elem(struct fsa_llvm_context *fsa,
                                   LLVMValueRef value, int index);

#endif