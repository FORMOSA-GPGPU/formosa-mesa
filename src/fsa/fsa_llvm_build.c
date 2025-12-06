#include "fsa_llvm_build.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
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

LLVMValueRef
fsa_get_pointer_val(struct fsa_llvm_context *ctx, LLVMTypeRef elem_type,
                    LLVMValueRef addr)
{
   LLVMTypeRef ptr_type = LLVMPointerType(elem_type, 0);
   LLVMValueRef input_ptr = LLVMBuildIntToPtr(ctx->builder, addr, ptr_type, "");
   return input_ptr;
}

LLVMValueRef
fsa_build_load_input_to_scalar(struct fsa_llvm_context *ctx, LLVMValueRef addr,
                               LLVMValueRef scalar)
{
   LLVMTypeRef dest_type = LLVMTypeOf(scalar);
   LLVMValueRef input_ptr = fsa_get_pointer_val(ctx, dest_type, addr);
   return LLVMBuildLoad2(ctx->builder, dest_type, input_ptr, "");
}

LLVMValueRef
fsa_build_load_input_to_vector(struct fsa_llvm_context *ctx, LLVMValueRef addr,
                               LLVMValueRef vec, LLVMValueRef idx)
{
   /* Assume `vec` is always VectorTypeKind */
   LLVMTypeRef dest_type = LLVMTypeOf(vec);
   LLVMTypeRef elem_type = LLVMGetElementType(dest_type);
   LLVMValueRef input_ptr = fsa_get_pointer_val(ctx, elem_type, addr);
   LLVMValueRef scalar_result =
      LLVMBuildLoad2(ctx->builder, elem_type, input_ptr, "");
   return LLVMBuildInsertElement(ctx->builder, vec, scalar_result, idx, "");
}

void
fsa_build_type_name_for_intr(LLVMTypeRef type, char *buf, unsigned bufsize)
{
   LLVMTypeRef elem_type = type;

   if (LLVMGetTypeKind(type) == LLVMStructTypeKind) {
      unsigned count = LLVMCountStructElementTypes(type);
      int ret = snprintf(buf, bufsize, "sl_");
      buf += ret;
      bufsize -= ret;

      LLVMTypeRef *elems = alloca(count * sizeof(LLVMTypeRef));
      LLVMGetStructElementTypes(type, elems);

      for (unsigned i = 0; i < count; i++) {
         fsa_build_type_name_for_intr(elems[i], buf, bufsize);
         ret = strlen(buf);
         buf += ret;
         bufsize -= ret;
      }

      snprintf(buf, bufsize, "s");
      return;
   }

   assert(bufsize >= 8);
   if (LLVMGetTypeKind(type) == LLVMVectorTypeKind) {
      int ret = snprintf(buf, bufsize, "v%u", LLVMGetVectorSize(type));
      if (ret < 0) {
         char *type_name = LLVMPrintTypeToString(type);
         fprintf(stderr, "Error building type name for: %s\n", type_name);
         LLVMDisposeMessage(type_name);
         return;
      }
      elem_type = LLVMGetElementType(type);
      buf += ret;
      bufsize -= ret;
   }
   switch (LLVMGetTypeKind(elem_type)) {
   default:
      break;
   case LLVMIntegerTypeKind:
      snprintf(buf, bufsize, "i%d", LLVMGetIntTypeWidth(elem_type));
      break;
   case LLVMHalfTypeKind:
      snprintf(buf, bufsize, "f16");
      break;
   case LLVMFloatTypeKind:
      snprintf(buf, bufsize, "f32");
      break;
   case LLVMDoubleTypeKind:
      snprintf(buf, bufsize, "f64");
      break;
   }
}

LLVMValueRef
fsa_build_const_int_vec(struct fsa_llvm_context *ctx, LLVMTypeRef type,
                        long long val, bool sign_extend)
{
   if (LLVMGetTypeKind(type) == LLVMVectorTypeKind) {
      LLVMValueRef scalar =
         LLVMConstInt(LLVMGetElementType(type), val, sign_extend);
      unsigned vec_size = LLVMGetVectorSize(type);
      LLVMValueRef *scalars = alloca(vec_size * sizeof(LLVMValueRef));

      for (unsigned i = 0; i < vec_size; i++)
         scalars[i] = scalar;
      return LLVMConstVector(scalars, vec_size);
   }
   return LLVMConstInt(type, val, 0);
}

LLVMValueRef
fsa_const_uint_vec(struct fsa_llvm_context *ctx, LLVMTypeRef type,
                   uint64_t value)
{

   if (LLVMGetTypeKind(type) == LLVMVectorTypeKind) {
      LLVMValueRef scalar = LLVMConstInt(LLVMGetElementType(type), value, 0);
      unsigned vec_size = LLVMGetVectorSize(type);
      LLVMValueRef *scalars = alloca(vec_size * sizeof(LLVMValueRef));

      for (unsigned i = 0; i < vec_size; i++)
         scalars[i] = scalar;
      return LLVMConstVector(scalars, vec_size);
   }
   return LLVMConstInt(type, value, 0);
}

LLVMValueRef
fsa_build_floating_reciprocal(struct fsa_llvm_context *ctx, LLVMValueRef value)
{
   LLVMValueRef f32_1 = ctx->f32_1;
   LLVMValueRef LHS = f32_1;
   if (LLVMGetTypeKind(LLVMTypeOf(value)) == LLVMVectorTypeKind) {
      LLVMValueRef elems[4];
      unsigned vsize = LLVMGetVectorSize(LLVMTypeOf(value));
      for (unsigned i = 0; i < vsize; i++) {
         elems[i] = f32_1;
      }
      LHS = LLVMConstVector(elems, vsize);
   }
   return LLVMBuildFDiv(ctx->builder, LHS, value, "");
}

LLVMValueRef
fsa_build_fmin(struct fsa_llvm_context *ctx, LLVMValueRef a, LLVMValueRef b)
{
   char name[64], type[64];

   fsa_build_type_name_for_intr(LLVMTypeOf(a), type, sizeof(type));
   snprintf(name, sizeof(name), "llvm.minnum.%s", type);
   LLVMValueRef args[2] = {a, b};
   return fsa_build_intrinsic(ctx, name, LLVMTypeOf(a), args, 2, 0);
}

LLVMValueRef
fsa_build_fmax(struct fsa_llvm_context *ctx, LLVMValueRef a, LLVMValueRef b)
{
   char name[64], type[64];

   fsa_build_type_name_for_intr(LLVMTypeOf(a), type, sizeof(type));
   snprintf(name, sizeof(name), "llvm.maxnum.%s", type);
   LLVMValueRef args[2] = {a, b};
   return fsa_build_intrinsic(ctx, name, LLVMTypeOf(a), args, 2, 0);
}

LLVMValueRef
fsa_build_imin(struct fsa_llvm_context *ctx, LLVMValueRef a, LLVMValueRef b)
{
   LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntSLE, a, b, "");
   return LLVMBuildSelect(ctx->builder, cmp, a, b, "");
}

LLVMValueRef
fsa_build_imax(struct fsa_llvm_context *ctx, LLVMValueRef a, LLVMValueRef b)
{
   LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntSGT, a, b, "");
   return LLVMBuildSelect(ctx->builder, cmp, a, b, "");
}

LLVMValueRef
fsa_build_umin(struct fsa_llvm_context *ctx, LLVMValueRef a, LLVMValueRef b)
{
   LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntULE, a, b, "");
   return LLVMBuildSelect(ctx->builder, cmp, a, b, "");
}

LLVMValueRef
fsa_build_umax(struct fsa_llvm_context *ctx, LLVMValueRef a, LLVMValueRef b)
{
   LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntUGE, a, b, "");
   return LLVMBuildSelect(ctx->builder, cmp, a, b, "");
}

LLVMValueRef
fsa_build_clamp(struct fsa_llvm_context *ctx, LLVMValueRef value)
{
   LLVMTypeRef t = LLVMTypeOf(value);
   return fsa_build_fmin(ctx, fsa_build_fmax(ctx, value, LLVMConstReal(t, 0.0)),
                         LLVMConstReal(t, 1.0));
}

LLVMValueRef
fsa_build_fsat(struct fsa_llvm_context *ctx, LLVMValueRef src, LLVMTypeRef type)
{
   LLVMValueRef zero = LLVMConstReal(type, 0.0);
   LLVMValueRef one = LLVMConstReal(type, 1.0);
   LLVMValueRef result;

   result = fsa_build_fmin(ctx, fsa_build_fmax(ctx, src, zero), one);
   return result;
}

LLVMValueRef
fsa_build_isign(struct fsa_llvm_context *ctx, LLVMValueRef src0)
{
   LLVMTypeRef type = LLVMTypeOf(src0);
   LLVMValueRef val;

   val = fsa_build_imax(ctx, src0, fsa_const_uint_vec(ctx, type, -1));
   return fsa_build_imin(ctx, val, fsa_const_uint_vec(ctx, type, 1));
}

static LLVMValueRef
fsa_eliminate_negative_zero(struct fsa_llvm_context *ctx, LLVMValueRef val)
{
   /* I have ignored the [enable/disable]_signed_zeros functions, they are
    * located at ac_llvm_helper.cpp*/
   // fsa_enable_signed_zeros(ctx);
   /* (val + 0) converts negative zero to positive zero. */
   val = LLVMBuildFAdd(ctx->builder, val, LLVMConstNull(LLVMTypeOf(val)), "");
   // fsa_disable_signed_zeros(ctx);
   return val;
}

LLVMValueRef
fsa_build_fsign(struct fsa_llvm_context *ctx, LLVMValueRef src)
{
   LLVMTypeRef type = LLVMTypeOf(src);
   LLVMValueRef pos, neg, dw[2], val;
   unsigned bitsize = fsa_get_elem_bits(ctx, type);

   /* The standard version leads to this:
    *   v_cmp_ngt_f32_e64 s[0:1], s4, 0                       ; D40B0000
    * 00010004 v_cndmask_b32_e64 v4, 1.0, s4, s[0:1]                 ; D5010004
    * 000008F2 v_cmp_le_f32_e32 vcc, 0, v4                           ; 7C060880
    *   v_cndmask_b32_e32 v4, -1.0, v4, vcc                   ; 020808F3
    *
    * The isign version:
    *   v_add_f32_e64 v4, s4, 0                               ; D5030004
    * 00010004 v_med3_i32 v4, v4, -1, 1                              ; D5580004
    * 02058304 v_cvt_f32_i32_e32 v4, v4                              ; 7E080B04
    *
    * (src0 + 0) converts negative zero to positive zero.
    * After that, int(fsign(x)) == isign(floatBitsToInt(x)).
    *
    * For FP64, use the standard version, which doesn't suffer from the huge DP
    * rate reduction. (FP64 comparisons are as fast as int64 comparisons)
    */
   if (bitsize == 16 || bitsize == 32) {
      val = fsa_to_integer(ctx, fsa_eliminate_negative_zero(ctx, src));
      val = fsa_build_isign(ctx, val);
      return LLVMBuildSIToFP(ctx->builder, val, type, "");
   }

   assert(bitsize == 64);
   pos = LLVMBuildFCmp(ctx->builder, LLVMRealOGT, src, ctx->f64_0, "");
   neg = LLVMBuildFCmp(ctx->builder, LLVMRealOLT, src, ctx->f64_0, "");
   dw[0] = ctx->i32_0;
   dw[1] = LLVMBuildSelect(
      ctx->builder, pos, LLVMConstInt(ctx->i32, 0x3FF00000, 0),
      LLVMBuildSelect(ctx->builder, neg, LLVMConstInt(ctx->i32, 0xBFF00000, 0),
                      ctx->i32_0, ""),
      "");
   return LLVMBuildBitCast(ctx->builder, fsa_build_gather_values(ctx, dw, 2),
                           ctx->f64, "");
}

LLVMValueRef
fsa_build_ffract(struct fsa_llvm_context *ctx, LLVMValueRef src)
{
   char name[64], type[64];

   fsa_build_type_name_for_intr(LLVMTypeOf(src), type, sizeof(type));
   snprintf(name, sizeof(name), "llvm.floor.%s", type);

   LLVMValueRef args[1] = {src};
   LLVMValueRef floor_val =
      fsa_build_intrinsic(ctx, name, LLVMTypeOf(src), args, 1, 0);

   return LLVMBuildFSub(ctx->builder, src, floor_val, "");
}