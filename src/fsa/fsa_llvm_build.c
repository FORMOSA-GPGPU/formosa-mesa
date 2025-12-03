#include "fsa_llvm_build.h"
#include <stdbool.h>
#include <stdlib.h>
#include <util/macros.h>

void
fsa_llvm_context_init(struct fsa_llvm_context *ctx)
{
   ctx->context = LLVMContextCreate();
   /* amd set something else like target triple and data layout in a
    * `ac_create_module` */
   ctx->module = LLVMModuleCreateWithNameInContext("mesa-shader", ctx->context);
   /* amd set some floating point flags in `ac_create_builder` */
   ctx->builder = LLVMCreateBuilderInContext(ctx->context);

   ctx->voidt = LLVMVoidTypeInContext(ctx->context);
   ctx->i1 = LLVMInt1TypeInContext(ctx->context);
   ctx->i8 = LLVMInt8TypeInContext(ctx->context);
   ctx->i16 = LLVMInt16TypeInContext(ctx->context);
   ctx->i32 = LLVMInt32TypeInContext(ctx->context);
   ctx->i64 = LLVMInt64TypeInContext(ctx->context);
   ctx->f16 = LLVMHalfTypeInContext(ctx->context);
   ctx->f32 = LLVMFloatTypeInContext(ctx->context);
   ctx->f64 = LLVMDoubleTypeInContext(ctx->context);
   ctx->v2i32 = LLVMVectorType(ctx->i32, 2);
   ctx->v2i32 = LLVMVectorType(ctx->i32, 3);
   ctx->v4i32 = LLVMVectorType(ctx->i32, 4);
   ctx->v2f32 = LLVMVectorType(ctx->f32, 2);
   ctx->v2f32 = LLVMVectorType(ctx->f32, 3);
   ctx->v4f32 = LLVMVectorType(ctx->f32, 4);

   ctx->i8_0 = LLVMConstInt(ctx->i8, 0, false);
   ctx->i8_1 = LLVMConstInt(ctx->i8, 1, false);
   ctx->i16_0 = LLVMConstInt(ctx->i16, 0, false);
   ctx->i16_1 = LLVMConstInt(ctx->i16, 1, false);
   ctx->i32_0 = LLVMConstInt(ctx->i32, 0, false);
   ctx->i32_1 = LLVMConstInt(ctx->i32, 1, false);
   ctx->i64_0 = LLVMConstInt(ctx->i64, 0, false);
   ctx->i64_1 = LLVMConstInt(ctx->i64, 1, false);
   ctx->f32_0 = LLVMConstReal(ctx->f32, 0.0);
   ctx->f32_1 = LLVMConstReal(ctx->f32, 1.0);
   ctx->f64_0 = LLVMConstReal(ctx->f64, 0.0);
   ctx->f64_1 = LLVMConstReal(ctx->f64, 1.0);

   ctx->i1false = LLVMConstInt(ctx->i1, 0, false);
   ctx->i1true = LLVMConstInt(ctx->i1, 1, false);
}

struct fsa_llvm_pointer
fsa_build_main(struct fsa_llvm_context *ctx, const char *name,
               LLVMTypeRef ret_type, LLVMModuleRef module)
{
   LLVMTypeRef main_function_type = LLVMFunctionType(ret_type, NULL, 0, 0);
   LLVMValueRef main_function =
      LLVMAddFunction(module, name, main_function_type);
   LLVMBasicBlockRef main_function_body =
      LLVMAppendBasicBlockInContext(ctx->context, main_function, "main_body");
   LLVMPositionBuilderAtEnd(ctx->builder, main_function_body);

   /* Set function call convention ... ... see `ac_build_llvm.c` */

   ctx->main_function = (struct fsa_llvm_pointer){
      .value = main_function,
      .pointee_type = main_function_type,
   };

   return ctx->main_function;
}

int
fsa_get_llvm_num_components(LLVMValueRef value)
{
   LLVMTypeRef type = LLVMTypeOf(value);
   unsigned num_components =
      LLVMGetTypeKind(type) == LLVMVectorTypeKind ? LLVMGetVectorSize(type) : 1;
   return num_components;
}

LLVMValueRef
fsa_build_gather_values_extended(struct fsa_llvm_context *ctx,
                                 LLVMValueRef *values, unsigned value_count,
                                 unsigned value_stride, bool always_vector)
{
   LLVMBuilderRef builder = ctx->builder;
   LLVMValueRef vec = NULL;
   unsigned i;

   if (value_count == 1 && !always_vector) {
      return values[0];
   } else if (!value_count)
      unreachable("value_count is 0");

   for (i = 0; i < value_count; i++) {
      LLVMValueRef value = values[i * value_stride];

      if (!i)
         vec = LLVMGetUndef(LLVMVectorType(LLVMTypeOf(value), value_count));
      LLVMValueRef index = LLVMConstInt(ctx->i32, i, false);
      vec = LLVMBuildInsertElement(builder, vec, value, index, "");
   }
   return vec;
}

LLVMValueRef
fsa_build_gather_values(struct fsa_llvm_context *ctx, LLVMValueRef *values,
                        unsigned value_count)
{
   return fsa_build_gather_values_extended(ctx, values, value_count, 1, false);
}

void
fsa_print_llvm_type(const char *label, LLVMTypeRef t)
{
   char *str = LLVMPrintTypeToString(t);
   printf("[%s] Type Name: %s | Kind: %d\n", label, str, LLVMGetTypeKind(t));
   LLVMDisposeMessage(str); // Important: Free the string to avoid leaks
}

static LLVMTypeRef
to_float_type_scalar(struct fsa_llvm_context *ctx, LLVMTypeRef t)
{
   if (t == ctx->i8)
      return ctx->i8;
   else if (t == ctx->i16 || t == ctx->f16)
      return ctx->f16;
   else if (t == ctx->i32 || t == ctx->f32)
      return ctx->f32;
   else if (t == ctx->i64 || t == ctx->f64)
      return ctx->f64;
   else {
      unreachable("Unhandled float size");
   }
}

LLVMTypeRef
fsa_to_float_type(struct fsa_llvm_context *ctx, LLVMTypeRef t)
{
   if (LLVMGetTypeKind(t) == LLVMVectorTypeKind) {
      LLVMTypeRef elem_type = LLVMGetElementType(t);
      return LLVMVectorType(to_float_type_scalar(ctx, elem_type),
                            LLVMGetVectorSize(t));
   }
   return to_float_type_scalar(ctx, t);
}

LLVMValueRef
fsa_to_float(struct fsa_llvm_context *ctx, LLVMValueRef v)
{
   LLVMTypeRef type = LLVMTypeOf(v);
   return LLVMBuildBitCast(ctx->builder, v, fsa_to_float_type(ctx, type), "");
}

LLVMValueRef
fsa_build_intrinsic(struct fsa_llvm_context *ctx, const char *name,
                    LLVMTypeRef return_type, LLVMValueRef *params,
                    unsigned param_count, unsigned attrib_mask)
{
   LLVMValueRef call;

   LLVMTypeRef param_types[32];
   assert(param_count <= 32);
   for (unsigned i = 0; i < param_count; ++i) {
      assert(params[i]);
      param_types[i] = LLVMTypeOf(params[i]);
   }

   LLVMTypeRef function_type =
      LLVMFunctionType(return_type, param_types, param_count, 0);
   LLVMValueRef function = LLVMGetNamedFunction(ctx->module, name);

   if (!function) {
      function = LLVMAddFunction(ctx->module, name, function_type);

      LLVMSetFunctionCallConv(function, LLVMCCallConv);
      LLVMSetLinkage(function, LLVMExternalLinkage);
   }

   call = LLVMBuildCall2(ctx->builder, function_type, function, params,
                         param_count, "");
   return call;
}

LLVMValueRef
fsa_build_canonicalize(struct fsa_llvm_context *ctx, LLVMValueRef src0,
                       unsigned bitsize)
{
   LLVMTypeRef type;
   char *intr;

   if (bitsize == 16) {
      intr = "llvm.canonicalize.f16";
      type = ctx->f16;
   } else if (bitsize == 32) {
      intr = "llvm.canonicalize.f32";
      type = ctx->f32;
   } else {
      intr = "llvm.canonicalize.f64";
      type = ctx->f64;
   }

   LLVMValueRef params[] = {
      src0,
   };
   return fsa_build_intrinsic(ctx, intr, type, params, 1, 0);
}

static LLVMTypeRef
to_integer_type_scalar(struct fsa_llvm_context *ctx, LLVMTypeRef t)
{
   if (t == ctx->i1)
      return ctx->i1;
   else if (t == ctx->i8)
      return ctx->i8;
   else if (t == ctx->f16 || t == ctx->i16)
      return ctx->i16;
   else if (t == ctx->f32 || t == ctx->i32)
      return ctx->i32;
   else if (t == ctx->f64 || t == ctx->i64)
      return ctx->i64;
   else
      unreachable("Unhandled integer size");
}

LLVMTypeRef
fsa_to_integer_type(struct fsa_llvm_context *ctx, LLVMTypeRef t)
{
   if (LLVMGetTypeKind(t) == LLVMVectorTypeKind) {
      LLVMTypeRef elem_type = LLVMGetElementType(t);
      return LLVMVectorType(to_integer_type_scalar(ctx, elem_type),
                            LLVMGetVectorSize(t));
   }
   if (LLVMGetTypeKind(t) == LLVMPointerTypeKind) {
      /*
      switch (LLVMGetPointerAddressSpace(t)) {
      case AC_ADDR_SPACE_GLOBAL:
      case AC_ADDR_SPACE_CONST:
         return ctx->i64;
      case AC_ADDR_SPACE_CONST_32BIT:
      case AC_ADDR_SPACE_LDS:
         return ctx->i32;
      default:
         unreachable("unhandled address space");
      }
      */

      /* We dont have separat address space now, maybe look into this */
      return ctx->i64;
   }
   return to_integer_type_scalar(ctx, t);
}

LLVMValueRef
fsa_to_integer(struct fsa_llvm_context *ctx, LLVMValueRef v)
{
   LLVMTypeRef type = LLVMTypeOf(v);
   if (LLVMGetTypeKind(type) == LLVMPointerTypeKind) {
      return LLVMBuildPtrToInt(ctx->builder, v, fsa_to_integer_type(ctx, type),
                               "");
   }
   return LLVMBuildBitCast(ctx->builder, v, fsa_to_integer_type(ctx, type), "");
}

LLVMValueRef
fsa_to_integer_or_pointer(struct fsa_llvm_context *ctx, LLVMValueRef v)
{
   LLVMTypeRef type = LLVMTypeOf(v);
   if (LLVMGetTypeKind(type) == LLVMPointerTypeKind)
      return v;
   return fsa_to_integer(ctx, v);
}

int
fsa_get_elem_bits(struct fsa_llvm_context *ctx, LLVMTypeRef type)
{
   if (LLVMGetTypeKind(type) == LLVMVectorTypeKind)
      type = LLVMGetElementType(type);

   if (LLVMGetTypeKind(type) == LLVMIntegerTypeKind)
      return LLVMGetIntTypeWidth(type);

   if (LLVMGetTypeKind(type) == LLVMPointerTypeKind) {
      return 64;
   }

   if (type == ctx->f16)
      return 16;
   if (type == ctx->f32)
      return 32;
   if (type == ctx->f64)
      return 64;

   unreachable("Unhandled type kind in get_elem_bits");
}

LLVMValueRef
fsa_llvm_extract_elem(struct fsa_llvm_context *fsa, LLVMValueRef value,
                      int index)
{
   if (LLVMGetTypeKind(LLVMTypeOf(value)) != LLVMVectorTypeKind) {
      assert(index == 0);
      return value;
   }

   return LLVMBuildExtractElement(fsa->builder, value,
                                  LLVMConstInt(fsa->i32, index, false), "");
}