#include "fsa_llvm_build.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <util/macros.h>

#define FSA_LLVM_INITIAL_CF_DEPTH 4
/* Data for if/else/endif and bgnloop/endloop control flow structures.
 */
struct fsa_llvm_flow {
   /* Loop exit or next part of if/else/endif. */
   LLVMBasicBlockRef next_block;
   LLVMBasicBlockRef loop_entry_block;
};

void
fsa_llvm_context_init(struct fsa_llvm_context *ctx)
{
   ctx->context = LLVMContextCreate();
   /* amd set something else like target triple and data layout in a
    * `ac_create_module` */
   ctx->module = LLVMModuleCreateWithNameInContext("mesa-shader", ctx->context);
   const char *data_layout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128";
   LLVMSetDataLayout(ctx->module, data_layout);
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
   ctx->v2i8 = LLVMVectorType(ctx->i8, 2);
   ctx->v3i8 = LLVMVectorType(ctx->i8, 3);
   ctx->v4i8 = LLVMVectorType(ctx->i8, 4);
   ctx->v2i16 = LLVMVectorType(ctx->i16, 2);
   ctx->v3i16 = LLVMVectorType(ctx->i16, 3);
   ctx->v4i16 = LLVMVectorType(ctx->i16, 4);
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

   ctx->ptrt = LLVMPointerTypeInContext(ctx->context, 0);
   /* gpu struct types */
   /*
   struct GfxLayout {
      float* base;
      uint64_t stride;
   };
   */
   ctx->gfx_layout_t = LLVMStructCreateNamed(ctx->context, "gfx.layout");
   LLVMTypeRef layout_layout[] = {
      ctx->ptrt,
      ctx->i64,
   };
   LLVMStructSetBody(ctx->gfx_layout_t, layout_layout, 2, 0);

   /*
   struct GfxState {
      // Vertex shader input
      struct GfxLayout vert_attrib_generic[16];
      // Vertex shader output, Fragment shader input
      struct GfxLayout varying_slot_pos[1];
      struct GfxLayout varying_slot_psize[1];
      struct GfxLayout varying_slot_tex[8];
      struct GfxLayout varying_slot_var[32];
      struct GfxLayout frag_result_data[8];
      float viewport_scale[3];
      float viewport_offset[3];
      void *ubo_base[16];
      // Visibility stream
      void* VisS_base;
   };
   */
   ctx->gfx_state_t = LLVMStructCreateNamed(ctx->context, "gfx.state");
   LLVMTypeRef vert_attrib_arr = LLVMArrayType(ctx->gfx_layout_t, 16);
   LLVMTypeRef varying_slot_pos_arr = LLVMArrayType(ctx->gfx_layout_t, 1);
   LLVMTypeRef varying_slot_psize_arr = LLVMArrayType(ctx->gfx_layout_t, 1);
   LLVMTypeRef varying_slot_tex_arr = LLVMArrayType(ctx->gfx_layout_t, 8);
   LLVMTypeRef varying_slot_var_arr = LLVMArrayType(ctx->gfx_layout_t, 32);
   LLVMTypeRef frag_result_data_arr = LLVMArrayType(ctx->gfx_layout_t, 8);
   LLVMTypeRef viewport_scale_arr = LLVMArrayType(ctx->f32, 3);
   LLVMTypeRef viewport_offset_arr = LLVMArrayType(ctx->f32, 3);
   /* Support only max 16 ubo */
   LLVMTypeRef ubo_base_arr = LLVMArrayType(ctx->ptrt, 16);
   LLVMTypeRef state_layout[] = {
      vert_attrib_arr,        varying_slot_pos_arr,
      varying_slot_psize_arr, varying_slot_tex_arr,
      varying_slot_var_arr,   frag_result_data_arr,
      viewport_scale_arr,     viewport_offset_arr,
      ubo_base_arr,           ctx->ptrt,
   };
   LLVMStructSetBody(ctx->gfx_state_t, state_layout,
                     sizeof(state_layout) / sizeof(LLVMTypeRef), 0);

   /*
   struct GfxVSInfo {
      struct GfxState* gfx_state;
      uint32_t wg_id;
      uint32_t wg_size;
   };
   */
   ctx->gfx_vs_info_t = LLVMStructCreateNamed(ctx->context, "gfx.vs_info");
   LLVMTypeRef vs_info_layout[] = {
      ctx->ptrt,
      ctx->i32,
      ctx->i32,
   };
   LLVMStructSetBody(ctx->gfx_vs_info_t, vs_info_layout,
                     sizeof(vs_info_layout) / sizeof(LLVMTypeRef), 0);

   /*
   struct GfxFSInfo {
      struct GfxState* gfx_state;
      uint32_t bin_id;
      uint32_t quad_id;
      bool pix_mask[4];
      ivec3 vert_idx;
      vec3 bary_coord[4];
   };
   */
   ctx->gfx_fs_info_t = LLVMStructCreateNamed(ctx->context, "gfx.fs_info");
   LLVMTypeRef fs_pix_mask_arr = LLVMArrayType(ctx->i1, 4);
   LLVMTypeRef fs_vert_idx_arr = LLVMArrayType(ctx->i32, 3);
   LLVMTypeRef fs_bary_coord = LLVMArrayType(ctx->f32, 3);
   LLVMTypeRef fs_bary_coord_arr = LLVMArrayType(fs_bary_coord, 4);
   LLVMTypeRef fs_info_layout[] = {
      ctx->ptrt,       ctx->i32,        ctx->i32,
      fs_pix_mask_arr, fs_vert_idx_arr, fs_bary_coord_arr,
   };
   LLVMStructSetBody(ctx->gfx_fs_info_t, fs_info_layout,
                     sizeof(fs_info_layout) / sizeof(LLVMTypeRef), 0);

   /* `calloc` init mem to zero */
   ctx->flow = calloc(1, sizeof(*ctx->flow));
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
   unsigned bitsize = fsa_get_elem_bits(ctx, LLVMTypeOf(src));

   /* result = (x > 0) ? 1.0 : ((x < 0) ? -1.0 : 0.0) */
   LLVMValueRef zero = bitsize == 32 ? ctx->f32_0 : ctx->f64_0;
   LLVMValueRef one = bitsize == 32 ? ctx->f32_1 : ctx->f64_1;
   LLVMValueRef neg_one =
      LLVMConstReal(bitsize == 32 ? ctx->f32 : ctx->f64, -1.0);
   LLVMValueRef is_pos =
      LLVMBuildFCmp(ctx->builder, LLVMRealOGT, src, zero, "");
   LLVMValueRef is_neg =
      LLVMBuildFCmp(ctx->builder, LLVMRealOLT, src, zero, "");
   LLVMValueRef neg_or_zero =
      LLVMBuildSelect(ctx->builder, is_neg, neg_one, zero, "");

   return LLVMBuildSelect(ctx->builder, is_pos, one, neg_or_zero, "");
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

LLVMValueRef
fsa_build_frexp_exp(struct fsa_llvm_context *ctx, LLVMValueRef src0,
                    unsigned bitsize)
{
   unsigned mantissa_bits, bias;
   unsigned long long exp_mask;
   LLVMTypeRef int_type;

   if (bitsize == 16) {
      mantissa_bits = 10;
      bias = 15;
      exp_mask = 0x1F;
      int_type = ctx->i16;
   } else if (bitsize == 32) {
      mantissa_bits = 23;
      bias = 127;
      exp_mask = 0xFF;
      int_type = ctx->i32;
   } else if (bitsize == 64) {
      mantissa_bits = 52;
      bias = 1023;
      exp_mask = 0x7FF;
      int_type = ctx->i64;
   } else {
      unreachable("Unsupported bitwidth");
   }

   LLVMValueRef val_as_int = fsa_to_integer(ctx, src0);

   LLVMValueRef shifted = LLVMBuildLShr(
      ctx->builder, val_as_int, LLVMConstInt(int_type, mantissa_bits, 0), "");

   LLVMValueRef raw_exp = LLVMBuildAnd(ctx->builder, shifted,
                                       LLVMConstInt(int_type, exp_mask, 0), "");

   /* Calc frexp exponent: (raw_exp - bias + 1) */
   LLVMValueRef exp_i32 = LLVMBuildIntCast(ctx->builder, raw_exp, ctx->i32, "");
   LLVMValueRef bias_val = LLVMConstInt(ctx->i32, bias, 0);
   LLVMValueRef one = ctx->i32_1;

   LLVMValueRef res = LLVMBuildSub(ctx->builder, exp_i32, bias_val, "");
   res = LLVMBuildAdd(ctx->builder, res, one, "");

   /* Handle edge case 0.0 */
   LLVMValueRef zero_float = LLVMConstNull(LLVMTypeOf(src0));
   LLVMValueRef is_zero =
      LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, src0, zero_float, "");

   LLVMValueRef tmp = LLVMBuildSelect(ctx->builder, is_zero,
                                      LLVMConstInt(ctx->i32, 0, 0), res, "");
   return tmp;
}

LLVMValueRef
fsa_build_frexp_mant(struct fsa_llvm_context *ctx, LLVMValueRef src0,
                     unsigned bitsize)
{
   unsigned mantissa_bits, bias;
   unsigned long long exp_mask; // Used to calculate the inverse mask
   LLVMTypeRef int_type;
   LLVMTypeRef float_type = LLVMTypeOf(src0);

   if (bitsize == 16) {
      mantissa_bits = 10;
      bias = 15;
      exp_mask = 0x1F;
      int_type = ctx->i16;
   } else if (bitsize == 32) {
      mantissa_bits = 23;
      bias = 127;
      exp_mask = 0xFF;
      int_type = ctx->i32;
   } else if (bitsize == 64) {
      mantissa_bits = 52;
      bias = 1023;
      exp_mask = 0x7FF;
      int_type = ctx->i64;
   } else {
      unreachable("Unsupported bitwidth");
   }

   LLVMValueRef val_as_int = fsa_to_integer(ctx, src0);

   /* * 1. Create a mask to PRESERVE the Sign and Mantissa.
    * We do this by inverting the exponent mask shifted into position.
    * preserve_mask = ~(exp_mask << mantissa_bits)
    */
   unsigned long long shifted_exp_mask = (unsigned long long)exp_mask
                                         << mantissa_bits;
   unsigned long long preserve_mask = ~shifted_exp_mask;

   /* * 2. Calculate the new exponent bits.
    * frexp returns [0.5, 1.0). In IEEE 754, this range corresponds
    * to an exponent of (bias - 1).
    */
   unsigned long long new_exp_bits = ((unsigned long long)(bias - 1))
                                     << mantissa_bits;

   // Clear the old exponent
   LLVMValueRef stripped = LLVMBuildAnd(
      ctx->builder, val_as_int, LLVMConstInt(int_type, preserve_mask, 0), "");

   // OR in the new exponent
   LLVMValueRef result_int = LLVMBuildOr(
      ctx->builder, stripped, LLVMConstInt(int_type, new_exp_bits, 0), "");

   LLVMValueRef res =
      LLVMBuildBitCast(ctx->builder, result_int, float_type, "");

   /* Handle edge case 0.0 (result should be 0.0, not 0.5) */
   LLVMValueRef zero_float = LLVMConstNull(float_type);
   LLVMValueRef is_zero =
      LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, src0, zero_float, "");

   return LLVMBuildSelect(ctx->builder, is_zero, zero_float, res, "");
}

/*
   unsigned base = src0;
   unsigned offset = src1 & 0x1F;
   unsigned bits = src2 & 0x1F;
   if (bits == 0) {
      dst = 0;
   } else if (offset + bits < 32) {
      dst = (base << (32 - bits - offset)) >> (32 - bits);
   } else {
      dst = base >> offset;
}
*/
LLVMValueRef
fsa_build_bfe(struct fsa_llvm_context *ctx, LLVMValueRef input,
              LLVMValueRef offset, LLVMValueRef width, bool is_signed)
{
   LLVMValueRef c0 = LLVMConstInt(ctx->i32, 0, 0);
   LLVMValueRef c1f = LLVMConstInt(ctx->i32, 0x1f, 0);
   LLVMValueRef c32 = LLVMConstInt(ctx->i32, 32, 0);

   offset = LLVMBuildAnd(ctx->builder, offset, c1f, "");
   width = LLVMBuildAnd(ctx->builder, width, c1f, "");

   /* Cond 1: bits == 0 */
   LLVMValueRef cond_bits_zero =
      LLVMBuildICmp(ctx->builder, LLVMIntEQ, width, c0, "");

   /* Cond 2: offset + width < 32 */
   LLVMValueRef sum_off_width = LLVMBuildAdd(ctx->builder, offset, width, "");
   LLVMValueRef cond_normal =
      LLVMBuildICmp(ctx->builder, LLVMIntULT, sum_off_width, c32, "");

   /* * Path A: Standard BFE (offset + bits < 32)
    * dst = (base << (32 - bits - offset)) >> (32 - bits)
    */
   LLVMValueRef shl_amt = LLVMBuildSub(ctx->builder, c32, sum_off_width, "");
   LLVMValueRef shr_amt_std = LLVMBuildSub(ctx->builder, c32, width, "");

   LLVMValueRef val_std = LLVMBuildShl(ctx->builder, input, shl_amt, "");
   if (is_signed) {
      val_std = LLVMBuildAShr(ctx->builder, val_std, shr_amt_std, "");
   } else {
      val_std = LLVMBuildLShr(ctx->builder, val_std, shr_amt_std, "");
   }

   /* * Path B: Overflow (offset + bits >= 32)
    * dst = base >> offset
    */
   LLVMValueRef val_overflow;
   if (is_signed) {
      val_overflow = LLVMBuildAShr(ctx->builder, input, offset, "");
   } else {
      val_overflow = LLVMBuildLShr(ctx->builder, input, offset, "");
   }
   /* Choose result based on condition */
   LLVMValueRef result =
      LLVMBuildSelect(ctx->builder, cond_normal, val_std, val_overflow, "");
   return LLVMBuildSelect(ctx->builder, cond_bits_zero, c0, result, "");
}

LLVMValueRef
fsa_build_bitfield_reverse(struct fsa_llvm_context *ctx, LLVMValueRef src0)
{
   LLVMValueRef result;
   unsigned bitsize;

   bitsize = fsa_get_elem_bits(ctx, LLVMTypeOf(src0));

   switch (bitsize) {
   case 64:
      result = fsa_build_intrinsic(ctx, "llvm.bitreverse.i64", ctx->i64,
                                   (LLVMValueRef[]){src0}, 1, 0);
      result = LLVMBuildTrunc(ctx->builder, result, ctx->i32, "");
      break;
   case 32:
      result = fsa_build_intrinsic(ctx, "llvm.bitreverse.i32", ctx->i32,
                                   (LLVMValueRef[]){src0}, 1, 0);
      break;
   case 16:
      result = fsa_build_intrinsic(ctx, "llvm.bitreverse.i16", ctx->i16,
                                   (LLVMValueRef[]){src0}, 1, 0);
      result = LLVMBuildZExt(ctx->builder, result, ctx->i32, "");
      break;
   case 8:
      result = fsa_build_intrinsic(ctx, "llvm.bitreverse.i8", ctx->i8,
                                   (LLVMValueRef[]){src0}, 1, 0);
      result = LLVMBuildZExt(ctx->builder, result, ctx->i32, "");
      break;
   default:
      unreachable("invalid bitsize");
      break;
   }

   return result;
}

LLVMValueRef
fsa_build_bit_count(struct fsa_llvm_context *ctx, LLVMValueRef src0)
{
   LLVMValueRef result;
   unsigned bitsize;

   bitsize = fsa_get_elem_bits(ctx, LLVMTypeOf(src0));

   switch (bitsize) {
   case 64:
      result = fsa_build_intrinsic(ctx, "llvm.ctpop.i64", ctx->i64,
                                   (LLVMValueRef[]){src0}, 1, 0);
      result = LLVMBuildTrunc(ctx->builder, result, ctx->i32, "");
      break;
   case 32:
      result = fsa_build_intrinsic(ctx, "llvm.ctpop.i32", ctx->i32,
                                   (LLVMValueRef[]){src0}, 1, 0);
      break;
   case 16:
      result = fsa_build_intrinsic(ctx, "llvm.ctpop.i16", ctx->i16,
                                   (LLVMValueRef[]){src0}, 1, 0);
      result = LLVMBuildZExt(ctx->builder, result, ctx->i32, "");
      break;
   case 8:
      result = fsa_build_intrinsic(ctx, "llvm.ctpop.i8", ctx->i8,
                                   (LLVMValueRef[]){src0}, 1, 0);
      result = LLVMBuildZExt(ctx->builder, result, ctx->i32, "");
      break;
   default:
      unreachable("invalid bitsize");
      break;
   }

   return result;
}

LLVMValueRef
fsa_find_lsb(struct fsa_llvm_context *ctx, LLVMTypeRef dst_type,
             LLVMValueRef src0)
{
   unsigned src0_bitsize = fsa_get_elem_bits(ctx, LLVMTypeOf(src0));
   const char *intrin_name;
   LLVMTypeRef type;
   LLVMValueRef zero;

   switch (src0_bitsize) {
   case 64:
      intrin_name = "llvm.cttz.i64";
      type = ctx->i64;
      zero = ctx->i64_0;
      break;
   case 32:
      intrin_name = "llvm.cttz.i32";
      type = ctx->i32;
      zero = ctx->i32_0;
      break;
   case 16:
      intrin_name = "llvm.cttz.i16";
      type = ctx->i16;
      zero = ctx->i16_0;
      break;
   case 8:
      intrin_name = "llvm.cttz.i8";
      type = ctx->i8;
      zero = ctx->i8_0;
      break;
   default:
      unreachable("invalid bitsize");
   }

   LLVMValueRef params[2] = {
      src0,

      /* The value of 1 means that ffs(x=0) = undef, so LLVM won't
       * add special code to check for x=0. The reason is that
       * the LLVM behavior for x=0 is different from what we
       * need here. However, LLVM also assumes that ffs(x) is
       * in [0, 31], but GLSL expects that ffs(0) = -1, so
       * a conditional assignment to handle 0 is still required.
       *
       * The hardware already implements the correct behavior.
       */
      ctx->i1true,
   };

   LLVMValueRef lsb = fsa_build_intrinsic(ctx, intrin_name, type, params, 2, 0);

   if (src0_bitsize == 64) {
      lsb = LLVMBuildTrunc(ctx->builder, lsb, ctx->i32, "");
   } else if (src0_bitsize < 32) {
      lsb = LLVMBuildSExt(ctx->builder, lsb, ctx->i32, "");
   }

   /* TODO: We need an intrinsic to skip this conditional. */
   /* Check for zero: */
   return LLVMBuildSelect(
      ctx->builder, LLVMBuildICmp(ctx->builder, LLVMIntEQ, src0, zero, ""),
      LLVMConstInt(ctx->i32, -1, 0), lsb, "");
}

LLVMValueRef
ac_build_umsb(struct fsa_llvm_context *ctx, LLVMValueRef arg,
              LLVMTypeRef dst_type, bool rev)
{
   const char *intrin_name;
   LLVMTypeRef type;
   LLVMValueRef highest_bit;
   LLVMValueRef zero;
   unsigned bitsize;

   bitsize = fsa_get_elem_bits(ctx, LLVMTypeOf(arg));
   switch (bitsize) {
   case 64:
      intrin_name = "llvm.ctlz.i64";
      type = ctx->i64;
      highest_bit = LLVMConstInt(ctx->i64, 63, false);
      zero = ctx->i64_0;
      break;
   case 32:
      intrin_name = "llvm.ctlz.i32";
      type = ctx->i32;
      highest_bit = LLVMConstInt(ctx->i32, 31, false);
      zero = ctx->i32_0;
      break;
   case 16:
      intrin_name = "llvm.ctlz.i16";
      type = ctx->i16;
      highest_bit = LLVMConstInt(ctx->i16, 15, false);
      zero = ctx->i16_0;
      break;
   case 8:
      intrin_name = "llvm.ctlz.i8";
      type = ctx->i8;
      highest_bit = LLVMConstInt(ctx->i8, 7, false);
      zero = ctx->i8_0;
      break;
   default:
      unreachable("invalid bitsize");
      break;
   }

   LLVMValueRef params[2] = {
      arg,
      ctx->i1true,
   };

   LLVMValueRef msb = fsa_build_intrinsic(ctx, intrin_name, type, params, 2, 0);

   if (!rev) {
      /* The HW returns the last bit index from MSB, but TGSI/NIR wants
       * the index from LSB. Invert it by doing "31 - msb". */
      msb = LLVMBuildSub(ctx->builder, highest_bit, msb, "");
   }

   if (bitsize == 64) {
      msb = LLVMBuildTrunc(ctx->builder, msb, ctx->i32, "");
   } else if (bitsize < 32) {
      msb = LLVMBuildSExt(ctx->builder, msb, ctx->i32, "");
   }

   /* check for zero */
   return LLVMBuildSelect(ctx->builder,
                          LLVMBuildICmp(ctx->builder, LLVMIntEQ, arg, zero, ""),
                          LLVMConstInt(ctx->i32, -1, true), msb, "");
}

LLVMValueRef
fsa_build_imsb(struct fsa_llvm_context *ctx, LLVMValueRef arg,
               LLVMTypeRef dst_type, bool rev)
{
   const char *intrin_name;
   LLVMTypeRef type;
   LLVMValueRef highest_bit;
   LLVMValueRef zero;
   unsigned bitsize;

   bitsize = fsa_get_elem_bits(ctx, LLVMTypeOf(arg));
   switch (bitsize) {
   case 64:
      intrin_name = "llvm.ctlz.i64";
      type = ctx->i64;
      highest_bit = LLVMConstInt(ctx->i64, 63, false);
      zero = ctx->i64_0;
      break;
   case 32:
      intrin_name = "llvm.ctlz.i32";
      type = ctx->i32;
      highest_bit = LLVMConstInt(ctx->i32, 31, false);
      zero = ctx->i32_0;
      break;
   case 16:
      intrin_name = "llvm.ctlz.i16";
      type = ctx->i16;
      highest_bit = LLVMConstInt(ctx->i16, 15, false);
      zero = ctx->i16_0;
      break;
   case 8:
      intrin_name = "llvm.ctlz.i8";
      type = ctx->i8;
      highest_bit = LLVMConstInt(ctx->i8, 7, false);
      zero = ctx->i8_0;
      break;
   default:
      unreachable("invalid bitsize");
      break;
   }

   /* For signed MSB:
    * If arg is positive, we look for the highest set bit.
    * If arg is negative, we look for the highest cleared bit (by inverting arg).
    */
   LLVMValueRef is_negative =
      LLVMBuildICmp(ctx->builder, LLVMIntSLT, arg, zero, "");
   LLVMValueRef not_arg = LLVMBuildNot(ctx->builder, arg, "");
   LLVMValueRef search_val =
      LLVMBuildSelect(ctx->builder, is_negative, not_arg, arg, "");

   LLVMValueRef params[2] = {
      search_val,
      ctx->i1true, /* is_zero_undef = true (handled manually below) */
   };

   /* This returns the number of leading zeros (from MSB) on the absolute-ish
    * value */
   LLVMValueRef msb = fsa_build_intrinsic(ctx, intrin_name, type, params, 2, 0);

   if (!rev) {
      /* The HW returns the last bit index from MSB, but TGSI/NIR wants
       * the index from LSB. Invert it by doing "bitsize - 1 - msb". */
      msb = LLVMBuildSub(ctx->builder, highest_bit, msb, "");
   }

   if (bitsize == 64) {
      msb = LLVMBuildTrunc(ctx->builder, msb, ctx->i32, "");
   } else if (bitsize < 32) {
      msb = LLVMBuildSExt(ctx->builder, msb, ctx->i32, "");
   }

   /* Edge case handling:
    * If arg is 0, search_val is 0.
    * If arg is -1, search_val is ~(-1) = 0.
    * In both cases, standard behavior returns -1.
    */
   LLVMValueRef is_zero =
      LLVMBuildICmp(ctx->builder, LLVMIntEQ, search_val, zero, "");

   return LLVMBuildSelect(ctx->builder, is_zero,
                          LLVMConstInt(ctx->i32, -1, true), msb, "");
}

static LLVMValueRef
fsa_build_cvt_pknorm_common(struct fsa_llvm_context *ctx, LLVMValueRef args[2],
                            bool is_signed)
{
   LLVMBuilderRef builder = ctx->builder;

   /* Define Constants */
   float min_v = is_signed ? -1.0f : 0.0f;
   float max_v = 1.0f;
   float scale_v = is_signed ? 32767.0f : 65535.0f;

   LLVMValueRef vec_min =
      LLVMConstVector((LLVMValueRef[]){LLVMConstReal(ctx->f32, min_v),
                                       LLVMConstReal(ctx->f32, min_v)},
                      2);
   LLVMValueRef vec_max =
      LLVMConstVector((LLVMValueRef[]){LLVMConstReal(ctx->f32, max_v),
                                       LLVMConstReal(ctx->f32, max_v)},
                      2);
   LLVMValueRef vec_scale =
      LLVMConstVector((LLVMValueRef[]){LLVMConstReal(ctx->f32, scale_v),
                                       LLVMConstReal(ctx->f32, scale_v)},
                      2);

   /* Create Vector from Input Args */
   LLVMValueRef vec = LLVMGetUndef(LLVMVectorType(ctx->f32, 2));
   vec = LLVMBuildInsertElement(builder, vec, args[0],
                                LLVMConstInt(ctx->i32, 0, 0), "");
   vec = LLVMBuildInsertElement(builder, vec, args[1],
                                LLVMConstInt(ctx->i32, 1, 0), "");

   /* Clamp (Min/Max) */
   // clamp(x, min, max) -> min(max(x, min), max)
   // Handle Max bound
   LLVMValueRef gt_max = LLVMBuildFCmp(builder, LLVMRealOGT, vec, vec_max, "");
   vec = LLVMBuildSelect(builder, gt_max, vec_max, vec, "");

   // Handle Min bound
   LLVMValueRef lt_min = LLVMBuildFCmp(builder, LLVMRealOLT, vec, vec_min, "");
   vec = LLVMBuildSelect(builder, lt_min, vec_min, vec, "");

   /* Scale */
   vec = LLVMBuildFMul(builder, vec, vec_scale, "");

   /* Round */
   // Use llvm.round intrinsic (Standard LLVM, distinct from AMD intrinsics)
   LLVMValueRef round_args[] = {vec};
   vec = fsa_build_intrinsic(ctx, "llvm.round.v2f32",
                             LLVMVectorType(ctx->f32, 2), round_args, 1, 0);

   /* Cast and Pack */
   LLVMTypeRef v2i16 = LLVMVectorType(ctx->i16, 2);
   LLVMValueRef res_vec;

   if (is_signed) {
      res_vec = LLVMBuildFPToSI(builder, vec, v2i16, "");
   } else {
      res_vec = LLVMBuildFPToUI(builder, vec, v2i16, "");
   }

   return LLVMBuildBitCast(builder, res_vec, ctx->i32, "");
}

LLVMValueRef
fsa_build_cvt_pknorm_i16(struct fsa_llvm_context *ctx, LLVMValueRef args[2])
{
   return fsa_build_cvt_pknorm_common(ctx, args, true);
}

LLVMValueRef
fsa_build_cvt_pknorm_u16(struct fsa_llvm_context *ctx, LLVMValueRef args[2])
{
   return fsa_build_cvt_pknorm_common(ctx, args, false);
}

static void
fsa_extend_args_f16_to_f32(struct fsa_llvm_context *ctx, LLVMValueRef src[2],
                           LLVMValueRef dst[2])
{
   LLVMBuilderRef builder = ctx->builder;
   dst[0] = LLVMBuildFPExt(builder, src[0], ctx->f32, "");
   dst[1] = LLVMBuildFPExt(builder, src[1], ctx->f32, "");
}

LLVMValueRef
fsa_build_cvt_pknorm_i16_f16(struct fsa_llvm_context *ctx, LLVMValueRef args[2])
{
   LLVMValueRef args_f32[2];
   fsa_extend_args_f16_to_f32(ctx, args, args_f32);

   // Reuse the f32 implementation
   return fsa_build_cvt_pknorm_i16(ctx, args_f32);
}

LLVMValueRef
fsa_build_cvt_pknorm_u16_f16(struct fsa_llvm_context *ctx, LLVMValueRef args[2])
{
   LLVMValueRef args_f32[2];
   fsa_extend_args_f16_to_f32(ctx, args, args_f32);

   // Reuse the f32 implementation
   return fsa_build_cvt_pknorm_u16(ctx, args_f32);
}

/* Helper to perform the bitwise packing: (src[0] & 0xFFFF) | (src[1] << 16) */
static LLVMValueRef
fsa_build_pack_2x16_common(struct fsa_llvm_context *ctx, LLVMValueRef args[2])
{
   LLVMBuilderRef builder = ctx->builder;

   // Constants
   LLVMValueRef mask_lo = LLVMConstInt(ctx->i32, 0xffff, 0);
   LLVMValueRef shift_hi = LLVMConstInt(ctx->i32, 16, 0);

   // Mask the lower 16 bits of the first component
   //    (src0 & 0xFFFF)
   LLVMValueRef lo = LLVMBuildAnd(builder, args[0], mask_lo, "");

   // Shift the second component into the upper 16 bits
   //    (src1 << 16)
   //    Note: Shifting left by 16 naturally discards the upper 16 bits of the
   //    input, so explicit masking is not strictly required if inputs are i32.
   LLVMValueRef hi = LLVMBuildShl(builder, args[1], shift_hi, "");

   // Combine them
   return LLVMBuildOr(builder, lo, hi, "");
}

/* Unsigned version: nir_op_pack_uint_2x16 */
LLVMValueRef
fsa_build_pack_uint_2x16(struct fsa_llvm_context *ctx, LLVMValueRef args[2])
{
   return fsa_build_pack_2x16_common(ctx, args);
}

/* Signed version: nir_op_pack_sint_2x16 */
LLVMValueRef
fsa_build_pack_sint_2x16(struct fsa_llvm_context *ctx, LLVMValueRef args[2])
{
   // In 2's complement arithmetic, truncation and packing are identical
   // for signed and unsigned integers.
   // e.g., -1 (0xFFFFFFFF) truncated to 16 bits is 0xFFFF.
   //       When unpacked back to i32 with sign-extension, it becomes 0xFFFFFFFF.
   return fsa_build_pack_2x16_common(ctx, args);
}

static void
fsa_build_extract_4x8(struct fsa_llvm_context *ctx, LLVMValueRef dest[4],
                      LLVMValueRef src, bool sign)
{
   LLVMValueRef sh_8 = LLVMConstInt(ctx->i32, 8, 0);
   LLVMValueRef sh_16 = LLVMConstInt(ctx->i32, 16, 0);
   LLVMValueRef sh_24 = LLVMConstInt(ctx->i32, 24, 0);
   dest[0] = LLVMBuildTrunc(ctx->builder, src, ctx->i8, "");
   dest[1] = LLVMBuildLShr(ctx->builder, src, sh_8, "");
   dest[2] = LLVMBuildLShr(ctx->builder, src, sh_16, "");
   dest[3] = LLVMBuildLShr(ctx->builder, src, sh_24, "");
   for (int i = 0; i < 4; i++) {
      dest[i] = LLVMBuildTrunc(ctx->builder, dest[i], ctx->i8, "");
      if (sign) {
         dest[i] = LLVMBuildSExt(ctx->builder, dest[i], ctx->i32, "");
      } else {
         dest[i] = LLVMBuildZExt(ctx->builder, dest[i], ctx->i32, "");
      }
   }
}

static LLVMValueRef
fsa_build_sdot_4x8_common(struct fsa_llvm_context *ctx, LLVMValueRef src0,
                          LLVMValueRef src1)
{
   /* Extract 8-bit values */
   LLVMValueRef src0_4x8[4] = {0};
   LLVMValueRef src1_4x8[4] = {0};
   fsa_build_extract_4x8(ctx, src0_4x8, src0, true);
   fsa_build_extract_4x8(ctx, src1_4x8, src1, true);

   LLVMValueRef dot[4] = {0};
   for (int i = 0; i < 4; i++) {
      dot[i] = LLVMBuildMul(ctx->builder, src0_4x8[i], src1_4x8[i], "");
   }

   LLVMValueRef tmp0 = LLVMBuildAdd(ctx->builder, dot[0], dot[1], "");
   LLVMValueRef tmp1 = LLVMBuildAdd(ctx->builder, dot[2], dot[3], "");
   return LLVMBuildAdd(ctx->builder, tmp0, tmp1, "");
}

LLVMValueRef
fsa_build_sdot_4x8_iadd(struct fsa_llvm_context *ctx, LLVMValueRef args[3])
{
   LLVMValueRef res = fsa_build_sdot_4x8_common(ctx, args[0], args[1]);
   res = LLVMBuildAdd(ctx->builder, res, args[2], "");
   return res;
}

LLVMValueRef
fsa_build_sdot_4x8_iadd_sat(struct fsa_llvm_context *ctx, LLVMValueRef args[3])
{
   LLVMValueRef res = fsa_build_sdot_4x8_common(ctx, args[0], args[1]);

   /* Saturating addition using llvm.sadd.sat.i32 intrinsic */
   LLVMValueRef sat_args[] = {res, args[2]};
   return fsa_build_intrinsic(ctx, "llvm.sadd.sat.i32", ctx->i32, sat_args, 2,
                              0);
}

/* Helper: Calculates the mixed-sign dot product of two packed <4 x i8> vectors.
 * Src0 is signed, Src1 is unsigned. Returns an i32 sum.
 */
static LLVMValueRef
fsa_build_sudot_4x8_common(struct fsa_llvm_context *ctx, LLVMValueRef src0,
                           LLVMValueRef src1)
{
   /* Extract 8-bit values */
   LLVMValueRef src0_4x8[4] = {0};
   LLVMValueRef src1_4x8[4] = {0};
   fsa_build_extract_4x8(ctx, src0_4x8, src0, true);
   fsa_build_extract_4x8(ctx, src1_4x8, src1, false);

   LLVMValueRef dot[4] = {0};
   for (int i = 0; i < 4; i++) {
      dot[i] = LLVMBuildMul(ctx->builder, src0_4x8[i], src1_4x8[i], "");
   }

   LLVMValueRef tmp0 = LLVMBuildAdd(ctx->builder, dot[0], dot[1], "");
   LLVMValueRef tmp1 = LLVMBuildAdd(ctx->builder, dot[2], dot[3], "");
   return LLVMBuildAdd(ctx->builder, tmp0, tmp1, "");
}

LLVMValueRef
fsa_build_sudot_4x8_iadd(struct fsa_llvm_context *ctx, LLVMValueRef args[3])
{
   LLVMValueRef dot = fsa_build_sudot_4x8_common(ctx, args[0], args[1]);

   return LLVMBuildAdd(ctx->builder, dot, args[2], "");
}

LLVMValueRef
fsa_build_sudot_4x8_iadd_sat(struct fsa_llvm_context *ctx, LLVMValueRef args[3])
{
   LLVMValueRef dot = fsa_build_sudot_4x8_common(ctx, args[0], args[1]);

   LLVMValueRef sat_args[] = {dot, args[2]};
   LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);

   return fsa_build_intrinsic(ctx, "llvm.sadd.sat.i32", i32, sat_args, 2, 0);
}

static LLVMValueRef
fsa_build_udot_4x8_common(struct fsa_llvm_context *ctx, LLVMValueRef src0,
                          LLVMValueRef src1)
{
   /* Extract 8-bit values */
   LLVMValueRef src0_4x8[4] = {0};
   LLVMValueRef src1_4x8[4] = {0};
   fsa_build_extract_4x8(ctx, src0_4x8, src0, false);
   fsa_build_extract_4x8(ctx, src1_4x8, src1, false);

   LLVMValueRef dot[4] = {0};
   for (int i = 0; i < 4; i++) {
      dot[i] = LLVMBuildMul(ctx->builder, src0_4x8[i], src1_4x8[i], "");
   }

   LLVMValueRef tmp0 = LLVMBuildAdd(ctx->builder, dot[0], dot[1], "");
   LLVMValueRef tmp1 = LLVMBuildAdd(ctx->builder, dot[2], dot[3], "");
   return LLVMBuildAdd(ctx->builder, tmp0, tmp1, "");
}

LLVMValueRef
fsa_build_udot_4x8_uadd(struct fsa_llvm_context *ctx, LLVMValueRef args[3])
{
   LLVMValueRef dot = fsa_build_udot_4x8_common(ctx, args[0], args[1]);
   return LLVMBuildAdd(ctx->builder, dot, args[2], "");
}

LLVMValueRef
fsa_build_udot_4x8_uadd_sat(struct fsa_llvm_context *ctx, LLVMValueRef args[3])
{
   LLVMValueRef dot = fsa_build_udot_4x8_common(ctx, args[0], args[1]);
   LLVMValueRef sat_args[] = {dot, args[2]};
   LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);

   return fsa_build_intrinsic(ctx, "llvm.uadd.sat.i32", i32, sat_args, 2, 0);
}

static void
fsa_build_extract_2x16(struct fsa_llvm_context *ctx, LLVMValueRef dest[2],
                       LLVMValueRef src, bool sign)
{
   LLVMValueRef sh_16 = LLVMConstInt(ctx->i32, 16, 0);
   dest[0] = LLVMBuildTrunc(ctx->builder, src, ctx->i16, "");
   dest[1] = LLVMBuildLShr(ctx->builder, src, sh_16, "");
   for (int i = 0; i < 2; i++) {
      dest[i] = LLVMBuildTrunc(ctx->builder, dest[i], ctx->i16, "");
      if (sign) {
         dest[i] = LLVMBuildSExt(ctx->builder, dest[i], ctx->i32, "");
      } else {
         dest[i] = LLVMBuildZExt(ctx->builder, dest[i], ctx->i32, "");
      }
   }
}

static LLVMValueRef
fsa_build_dot_2x16_common(struct fsa_llvm_context *ctx, LLVMValueRef src0,
                          LLVMValueRef src1, int is_signed)
{
   /* Extract 16-bit values */
   LLVMValueRef src0_2x16[2] = {0};
   LLVMValueRef src1_2x16[2] = {0};
   fsa_build_extract_2x16(ctx, src0_2x16, src0, is_signed);
   fsa_build_extract_2x16(ctx, src1_2x16, src1, is_signed);

   LLVMValueRef dot[2] = {0};
   for (int i = 0; i < 2; i++) {
      dot[i] = LLVMBuildMul(ctx->builder, src0_2x16[i], src1_2x16[i], "");
   }

   return LLVMBuildAdd(ctx->builder, dot[0], dot[1], "");
}

LLVMValueRef
fsa_build_sdot_2x16_iadd(struct fsa_llvm_context *ctx, LLVMValueRef args[3])
{
   LLVMValueRef dot = fsa_build_dot_2x16_common(ctx, args[0], args[1], 1);
   return LLVMBuildAdd(ctx->builder, dot, args[2], "");
}

LLVMValueRef
fsa_build_udot_2x16_uadd(struct fsa_llvm_context *ctx, LLVMValueRef args[3])
{
   LLVMValueRef dot = fsa_build_dot_2x16_common(ctx, args[0], args[1], 0);
   return LLVMBuildAdd(ctx->builder, dot, args[2], "");
}

LLVMValueRef
fsa_build_sdot_2x16_iadd_sat(struct fsa_llvm_context *ctx, LLVMValueRef args[3])
{
   LLVMValueRef dot = fsa_build_dot_2x16_common(ctx, args[0], args[1], 1);

   /* Saturating addition using llvm.sadd.sat.i32 intrinsic */
   LLVMValueRef sat_args[] = {dot, args[2]};
   LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
   return fsa_build_intrinsic(ctx, "llvm.sadd.sat.i32", i32, sat_args, 2, 0);
}

LLVMValueRef
fsa_build_udot_2x16_uadd_sat(struct fsa_llvm_context *ctx, LLVMValueRef args[3])
{
   LLVMValueRef dot = fsa_build_dot_2x16_common(ctx, args[0], args[1], 0);
   /* Saturating addition using llvm.uadd.sat.i32 intrinsic */
   LLVMValueRef sat_args[] = {dot, args[2]};
   LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);

   return fsa_build_intrinsic(ctx, "llvm.uadd.sat.i32", i32, sat_args, 2, 0);
}

static LLVMValueRef
fsa_build_msad_4x8_common(struct fsa_llvm_context *ctx, LLVMValueRef src0,
                          LLVMValueRef src1)
{
   LLVMValueRef src0_4x8[4], mask[4], absDiff[4];
   fsa_build_extract_4x8(ctx, src0_4x8, src0, false);
   fsa_build_extract_4x8(ctx, mask, src1, false);
   for (int i = 0; i < 4; i++) {
      absDiff[i] = LLVMBuildSub(ctx->builder, src0_4x8[i], mask[i], "");
      LLVMValueRef absArgs[2] = {absDiff[i], ctx->i1false};
      absDiff[i] =
         fsa_build_intrinsic(ctx, "llvm.abs.i32", ctx->i32, absArgs, 2, 0);
      absDiff[i] = LLVMBuildZExt(ctx->builder, absDiff[i], ctx->i32, "");
   }
   LLVMValueRef sum01 = LLVMBuildAdd(ctx->builder, absDiff[0], absDiff[1], "");
   LLVMValueRef sum23 = LLVMBuildAdd(ctx->builder, absDiff[2], absDiff[3], "");
   return LLVMBuildAdd(ctx->builder, sum01, sum23, "");
}

LLVMValueRef
fsa_build_msad_4x8(struct fsa_llvm_context *ctx, LLVMValueRef args[3])
{
   /* args[0] = src (packed u32)
    * args[1] = mask (packed u32)
    * args[2] = accum (u32)
    * Note: NIR order matches AMD: src, mask, accum
    */

   LLVMValueRef msad_val = fsa_build_msad_4x8_common(ctx, args[0], args[1]);
   return LLVMBuildAdd(ctx->builder, msad_val, args[2], "");
}

LLVMValueRef
fsa_build_mqsad_4x8(struct fsa_llvm_context *ctx, LLVMValueRef args[3])
{
   LLVMBuilderRef builder = ctx->builder;
   LLVMContextRef context = ctx->context;

   LLVMTypeRef i32 = LLVMInt32TypeInContext(context);
   LLVMTypeRef i64 = LLVMInt64TypeInContext(context);
   LLVMTypeRef v4i32 = LLVMVectorType(i32, 4);

   /* --- Sanitize Source (args[0]) to i64 --- */
   LLVMValueRef src_raw = args[0];
   LLVMValueRef src_u64;
   LLVMTypeRef src_type = LLVMTypeOf(src_raw);

   if (LLVMGetTypeKind(src_type) == LLVMVectorTypeKind) {
      /* If passed as <2 x i32>, bitcast to i64 */
      src_u64 = LLVMBuildBitCast(builder, src_raw, i64, "");
   } else if (LLVMGetIntTypeWidth(src_type) == 32) {
      /* If passed as i32 (wrong NIR def), ZExt to i64 so we have space to slide
       */
      src_u64 = LLVMBuildZExt(builder, src_raw, i64, "");
   } else {
      /* Already i64 */
      src_u64 = src_raw;
   }

   /* --- Sanitize Mask (args[1]) to i32 --- */
   LLVMValueRef mask_raw = args[1];
   LLVMValueRef mask_u32;
   LLVMTypeRef mask_type = LLVMTypeOf(mask_raw);

   if (LLVMGetTypeKind(mask_type) == LLVMVectorTypeKind) {
      /* If passed as <2 x i32>, it's too big. Extract first element or Trunc?
       * Usually implies wrong NIR type. We take the lower 32 bits via
       * bitcast->trunc */
      LLVMValueRef tmp = LLVMBuildBitCast(builder, mask_raw, i64, "");
      mask_u32 = LLVMBuildTrunc(builder, tmp, i32, "");
   } else if (LLVMGetIntTypeWidth(mask_type) == 64) {
      mask_u32 = LLVMBuildTrunc(builder, mask_raw, i32, "");
   } else {
      mask_u32 = mask_raw;
   }

   /* --- Sanitize Accumulator (args[2]) to <4 x i32> --- */
   LLVMValueRef accum_vec = args[2];
   if (LLVMGetTypeKind(LLVMTypeOf(accum_vec)) != LLVMVectorTypeKind) {
      /* If NIR passes it as a flat array or struct, cast it */
      accum_vec = LLVMBuildBitCast(builder, accum_vec, v4i32, "");
   }

   LLVMValueRef results[4];

   for (int i = 0; i < 4; ++i) {
      /* Fix for lshr error: shift amount must match src type (i64) */
      unsigned long long shift_val = i * 8;
      LLVMValueRef shift_amt = LLVMConstInt(i64, shift_val, 0);

      /* Now safe: i64 lshr i64 */
      LLVMValueRef shifted_src = LLVMBuildLShr(builder, src_u64, shift_amt, "");

      /* Truncate to i32 to get the low 4 bytes for MSAD */
      LLVMValueRef window_u32 = LLVMBuildTrunc(builder, shifted_src, i32, "");

      /* Compute MSAD for this window (helper expects i32, i32) */
      LLVMValueRef msad = fsa_build_msad_4x8_common(ctx, window_u32, mask_u32);

      /* Add to accumulator */
      LLVMValueRef acc_elem = LLVMBuildExtractElement(
         builder, accum_vec, LLVMConstInt(i32, i, 0), "");
      results[i] = LLVMBuildAdd(builder, msad, acc_elem, "");
   }

   /* Pack results */
   LLVMValueRef res_vec = LLVMGetUndef(v4i32);
   for (int i = 0; i < 4; ++i) {
      res_vec = LLVMBuildInsertElement(builder, res_vec, results[i],
                                       LLVMConstInt(i32, i, 0), "");
   }

   return res_vec;
}

static struct fsa_llvm_flow *
push_flow(struct fsa_llvm_context *ctx)
{
   struct fsa_llvm_flow *flow;

   if (ctx->flow->depth >= ctx->flow->depth_max) {
      unsigned new_max = MAX2(ctx->flow->depth << 1, FSA_LLVM_INITIAL_CF_DEPTH);

      ctx->flow->stack =
         realloc(ctx->flow->stack, new_max * sizeof(*ctx->flow->stack));
      ctx->flow->depth_max = new_max;
   }

   flow = &ctx->flow->stack[ctx->flow->depth];
   ctx->flow->depth++;

   flow->next_block = NULL;
   flow->loop_entry_block = NULL;
   return flow;
}

/* Append a basic block at the level of the parent flow.
 */
static LLVMBasicBlockRef
append_basic_block(struct fsa_llvm_context *ctx, const char *name)
{
   assert(ctx->flow->depth >= 1);

   if (ctx->flow->depth >= 2) {
      struct fsa_llvm_flow *flow = &ctx->flow->stack[ctx->flow->depth - 2];

      return LLVMInsertBasicBlockInContext(ctx->context, flow->next_block,
                                           name);
   }

   LLVMValueRef main_fn =
      LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
   return LLVMAppendBasicBlockInContext(ctx->context, main_fn, name);
}

static void
set_basicblock_name(LLVMBasicBlockRef bb, const char *base, int label_id)
{
   char buf[32];
   snprintf(buf, sizeof(buf), "%s%d", base, label_id);
   LLVMSetValueName(LLVMBasicBlockAsValue(bb), buf);
}

void
fsa_build_ifcc(struct fsa_llvm_context *ctx, LLVMValueRef cond, int label_id)
{
   struct fsa_llvm_flow *flow = push_flow(ctx);
   LLVMBasicBlockRef if_block;

   if_block = append_basic_block(ctx, "IF");
   flow->next_block = append_basic_block(ctx, "ELSE");
   set_basicblock_name(if_block, "if", label_id);
   LLVMBuildCondBr(ctx->builder, cond, if_block, flow->next_block);
   LLVMPositionBuilderAtEnd(ctx->builder, if_block);
}

static struct fsa_llvm_flow *
get_current_flow(struct fsa_llvm_context *ctx)
{
   if (ctx->flow->depth > 0)
      return &ctx->flow->stack[ctx->flow->depth - 1];
   return NULL;
}

/* Emit a branch to the given default target for the current block if
 * applicable -- that is, if the current block does not already contain a
 * branch from a break or continue.
 */
static void
emit_default_branch(LLVMBuilderRef builder, LLVMBasicBlockRef target)
{
   if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)))
      LLVMBuildBr(builder, target);
}

void
fsa_build_else(struct fsa_llvm_context *ctx, int label_id)
{
   struct fsa_llvm_flow *current_branch = get_current_flow(ctx);
   LLVMBasicBlockRef endif_block;

   assert(!current_branch->loop_entry_block);

   endif_block = append_basic_block(ctx, "ENDIF");
   emit_default_branch(ctx->builder, endif_block);

   LLVMPositionBuilderAtEnd(ctx->builder, current_branch->next_block);
   set_basicblock_name(current_branch->next_block, "else", label_id);

   current_branch->next_block = endif_block;
}

void
fsa_build_endif(struct fsa_llvm_context *ctx, int label_id)
{
   struct fsa_llvm_flow *current_branch = get_current_flow(ctx);

   assert(!current_branch->loop_entry_block);

   emit_default_branch(ctx->builder, current_branch->next_block);
   LLVMPositionBuilderAtEnd(ctx->builder, current_branch->next_block);
   set_basicblock_name(current_branch->next_block, "endif", label_id);

   ctx->flow->depth--;
}

void
fsa_build_bgnloop(struct fsa_llvm_context *ctx, int label_id)
{
   struct fsa_llvm_flow *flow = push_flow(ctx);
   flow->loop_entry_block = append_basic_block(ctx, "LOOP");
   flow->next_block = append_basic_block(ctx, "ENDLOOP");
   set_basicblock_name(flow->loop_entry_block, "loop", label_id);
   LLVMBuildBr(ctx->builder, flow->loop_entry_block);
   LLVMPositionBuilderAtEnd(ctx->builder, flow->loop_entry_block);
}

void
fsa_build_endloop(struct fsa_llvm_context *ctx, int label_id)
{
   struct fsa_llvm_flow *current_loop = get_current_flow(ctx);

   assert(current_loop->loop_entry_block);

   emit_default_branch(ctx->builder, current_loop->loop_entry_block);

   LLVMPositionBuilderAtEnd(ctx->builder, current_loop->next_block);
   set_basicblock_name(current_loop->next_block, "endloop", label_id);
   ctx->flow->depth--;
}

static struct fsa_llvm_flow *
get_innermost_loop(struct fsa_llvm_context *ctx)
{
   for (unsigned i = ctx->flow->depth; i > 0; --i) {
      if (ctx->flow->stack[i - 1].loop_entry_block)
         return &ctx->flow->stack[i - 1];
   }
   return NULL;
}

void
fsa_build_break(struct fsa_llvm_context *ctx)
{
   struct fsa_llvm_flow *flow = get_innermost_loop(ctx);
   LLVMBuildBr(ctx->builder, flow->next_block);
}

void
fsa_build_continue(struct fsa_llvm_context *ctx)
{
   struct fsa_llvm_flow *flow = get_innermost_loop(ctx);
   LLVMBuildBr(ctx->builder, flow->loop_entry_block);
}

LLVMValueRef
fsa_build_info_ptr(struct fsa_llvm_context *ctx)
{
   LLVMValueRef info_ptr_val = fsa_build_csr_read(ctx, "mscratch");
   return LLVMBuildIntToPtr(ctx->builder, info_ptr_val, ctx->ptrt, "info_ptr");
}

LLVMValueRef
fsa_build_get_gfx_info_member_ptr(struct fsa_llvm_context *ctx, int idx,
                                  fsa_info_t info_type)
{
   LLVMValueRef ptr = fsa_build_info_ptr(ctx);
   if (info_type == fsa_vs_info) {
      return LLVMBuildStructGEP2(ctx->builder, ctx->gfx_vs_info_t, ptr, idx,
                                 "info_member_ptr");

   } else if (info_type == fsa_fs_info) {
      return LLVMBuildStructGEP2(ctx->builder, ctx->gfx_fs_info_t, ptr, idx,
                                 "info_member_ptr");
   }
   assert("Should never go here" && 0);
}

LLVMValueRef
fsa_build_get_gfx_state_member_ptr(struct fsa_llvm_context *ctx,
                                   fsa_gfx_state_member member,
                                   fsa_info_t info_type)
{
   LLVMValueRef state_ptr =
      fsa_build_get_gfx_info_member_ptr(ctx, 0, info_type);
   LLVMValueRef state_ptr_val =
      LLVMBuildLoad2(ctx->builder, ctx->ptrt, state_ptr, "gfxstate_val");
   return LLVMBuildStructGEP2(ctx->builder, ctx->gfx_state_t, state_ptr_val,
                              member, "gfxstate_member_ptr");
}

LLVMValueRef
fsa_build_get_gfx_state_arr_member_ptr(struct fsa_llvm_context *ctx,
                                       fsa_gfx_state_member member,
                                       LLVMTypeRef type, int idx,
                                       fsa_info_t info_type)
{
   LLVMValueRef state_member_arr_ptr =
      fsa_build_get_gfx_state_member_ptr(ctx, member, info_type);
   LLVMValueRef indices = LLVMConstInt(ctx->i64, idx, 0);
   return LLVMBuildGEP2(ctx->builder, type, state_member_arr_ptr, &indices, 1,
                        "gfxstate_member_idx_ptr");
}

LLVMValueRef
fsa_build_get_gfx_layout_base(struct fsa_llvm_context *ctx,
                              fsa_gfx_state_member member, int idx,
                              fsa_info_t info_type)
{
   LLVMValueRef layout_ptr = fsa_build_get_gfx_state_arr_member_ptr(
      ctx, member, ctx->gfx_layout_t, idx, info_type);
   /* gfx_state member stores array of gfx_layout, the returned ptr points to
    * the layout, so no additional BuildLoad2 is required to get the base and
    * stride */
   LLVMValueRef base_ptr = LLVMBuildStructGEP2(
      ctx->builder, ctx->gfx_layout_t, layout_ptr, 0, "layout_base_ptr");
   return LLVMBuildLoad2(ctx->builder, ctx->i64, base_ptr, "layout_base_val");
}

LLVMValueRef
fsa_build_get_gfx_layout_stride(struct fsa_llvm_context *ctx,
                                fsa_gfx_state_member member, int idx,
                                fsa_info_t info_type)
{
   LLVMValueRef layout_ptr = fsa_build_get_gfx_state_arr_member_ptr(
      ctx, member, ctx->gfx_layout_t, idx, info_type);
   LLVMValueRef stride_ptr = LLVMBuildStructGEP2(
      ctx->builder, ctx->gfx_layout_t, layout_ptr, 1, "layout_stride_ptr");
   return LLVMBuildLoad2(ctx->builder, ctx->i64, stride_ptr,
                         "layout_stride_val");
}

LLVMValueRef
fsa_build_get_viewport_scale(struct fsa_llvm_context *ctx, int idx)
{
   idx /= 4;
   assert(idx <= 3);
   LLVMValueRef scale_ptr = fsa_build_get_gfx_state_arr_member_ptr(
      ctx, fsa_viewport_scale, ctx->f32, idx, fsa_vs_info);
   return LLVMBuildLoad2(ctx->builder, ctx->f32, scale_ptr, "viewport_scale");
}

LLVMValueRef
fsa_build_get_viewport_offset(struct fsa_llvm_context *ctx, int idx)
{
   idx /= 4;
   assert(idx <= 3);
   LLVMValueRef offset_ptr = fsa_build_get_gfx_state_arr_member_ptr(
      ctx, fsa_viewport_offset, ctx->f32, idx, fsa_vs_info);
   return LLVMBuildLoad2(ctx->builder, ctx->f32, offset_ptr, "viewport_offset");
}

LLVMValueRef
fsa_build_get_uniform(struct fsa_llvm_context *ctx, uint64_t offset)
{
   /* uniform will be lowered to ubo accesses */
   assert("Uniform will be lowered to ubo accesses " && 0);
}

LLVMValueRef
fsa_build_csr_read(struct fsa_llvm_context *ctx, const char *csr_name)
{
   LLVMTypeRef func_type = LLVMFunctionType(ctx->i64, NULL, 0, 0);
   char asm_string[64] = {0};
   snprintf(asm_string, 64, "csrr $0, %s", csr_name);
   const char *constraint = "=r";
   LLVMValueRef inline_asm =
      LLVMGetInlineAsm(func_type, asm_string, strlen(asm_string), constraint,
                       strlen(constraint), 0, 0, LLVMInlineAsmDialectATT, 0);
   LLVMValueRef asm_call =
      LLVMBuildCall2(ctx->builder, func_type, inline_asm, NULL, 0, "");
   /* Add attributes `memory(none)` to callsite so that llvm knows there is no
    * memory state change for this asm call during this function */
   unsigned mem_kind = LLVMGetEnumAttributeKindForName("memory", 6);
   LLVMAttributeRef mem_attr =
      LLVMCreateEnumAttribute(ctx->context, mem_kind, 0);
   LLVMAddCallSiteAttribute(asm_call, LLVMAttributeFunctionIndex, mem_attr);
   unsigned unwind_kind = LLVMGetEnumAttributeKindForName("nounwind", 8);
   LLVMAttributeRef unwind_attr =
      LLVMCreateEnumAttribute(ctx->context, unwind_kind, 0);
   LLVMAddCallSiteAttribute(asm_call, LLVMAttributeFunctionIndex, unwind_attr);
   return asm_call;
}

LLVMValueRef
fsa_build_local_id(struct fsa_llvm_context *ctx)
{
   return fsa_build_csr_read(ctx, "mhartid");
}

static LLVMValueRef
build_vs_global_id(struct fsa_llvm_context *ctx)
{
   LLVMValueRef wg_id_ptr =
      fsa_build_get_gfx_info_member_ptr(ctx, 1, fsa_vs_info);
   LLVMValueRef wg_id = LLVMBuildLoad2(ctx->builder, ctx->i32, wg_id_ptr, "");
   LLVMValueRef wg_size_ptr =
      fsa_build_get_gfx_info_member_ptr(ctx, 2, fsa_vs_info);
   LLVMValueRef wg_size =
      LLVMBuildLoad2(ctx->builder, ctx->i32, wg_size_ptr, "");
   wg_id = LLVMBuildZExt(ctx->builder, wg_id, ctx->i64, "");
   wg_size = LLVMBuildZExt(ctx->builder, wg_size, ctx->i64, "");
   LLVMValueRef local_id = fsa_build_local_id(ctx);
   LLVMValueRef id_x_size = LLVMBuildMul(ctx->builder, wg_id, wg_size, "");
   LLVMValueRef global_id = LLVMBuildAdd(ctx->builder, id_x_size, local_id, "");
   return global_id;
}

static LLVMValueRef
build_fs_global_id(struct fsa_llvm_context *ctx)
{
   /* Fragment shader have only one warp per launch, global_id == local_id */
   return fsa_build_local_id(ctx);
}

LLVMValueRef
fsa_build_global_id(struct fsa_llvm_context *ctx, fsa_info_t info_type)
{
   if (info_type == fsa_vs_info) {
      return build_vs_global_id(ctx);
   } else if (info_type == fsa_fs_info) {
      return build_fs_global_id(ctx);
   }
   assert("Should never go here" && 0);
}

/*
   +---+---+
   | 2 | 3 |
   +---+---+
   | 0 | 1 |
   +---+---+
   frag coord x = quad_x_base + (tid % 2)
   frag coord y = quad_y_base + (tid >= 2)
*/
LLVMValueRef
fsa_build_load_frag_coord_x(struct fsa_llvm_context *ctx)
{
   /* Get global quad_id */
   LLVMValueRef quad_id_ptr =
      fsa_build_get_gfx_info_member_ptr(ctx, fsa_fsinfo_quad_id, fsa_fs_info);
   LLVMValueRef quad_id =
      LLVMBuildLoad2(ctx->builder, ctx->i32, quad_id_ptr, "quad_id");
   /* Decode screen space position x from quad_id % 320, quad is 2x2 */
   LLVMValueRef quad_x_num = LLVMConstInt(ctx->i32, 640 / 2, 0);
   LLVMValueRef quad_x_mod =
      LLVMBuildURem(ctx->builder, quad_id, quad_x_num, "quad_x_mod");
   LLVMValueRef quad_x_base = LLVMBuildMul(
      ctx->builder, quad_x_mod, LLVMConstInt(ctx->i32, 2, 0), "quad_x_base");
   /* If tid % 2 == 1, add one to x */
   LLVMValueRef thread_id = LLVMBuildTrunc(
      ctx->builder, fsa_build_local_id(ctx), ctx->i32, "tid_i32");
   LLVMValueRef addend = LLVMBuildAnd(
      ctx->builder, thread_id, LLVMConstInt(ctx->i32, 1, 0), "pos_x_addend");
   LLVMValueRef pos_x =
      LLVMBuildAdd(ctx->builder, quad_x_base, addend, "pos_x");
   return pos_x;
}

LLVMValueRef
fsa_build_load_frag_coord_y(struct fsa_llvm_context *ctx)
{
   /* Get global quad_id */
   LLVMValueRef quad_id_ptr =
      fsa_build_get_gfx_info_member_ptr(ctx, fsa_fsinfo_quad_id, fsa_fs_info);
   LLVMValueRef quad_id =
      LLVMBuildLoad2(ctx->builder, ctx->i32, quad_id_ptr, "quad_id");
   /* Decode screen space position x from quad_id / 320, quad is 2x2 */
   LLVMValueRef quad_y_num = LLVMConstInt(ctx->i32, 640 / 2, 0);
   LLVMValueRef quad_y_div =
      LLVMBuildUDiv(ctx->builder, quad_id, quad_y_num, "quad_y_div");
   LLVMValueRef quad_y_base = LLVMBuildMul(
      ctx->builder, quad_y_div, LLVMConstInt(ctx->i32, 2, 0), "quad_y_base");
   LLVMValueRef thread_id = LLVMBuildTrunc(
      ctx->builder, fsa_build_local_id(ctx), ctx->i32, "tid_i32");
   LLVMValueRef tid_cmp =
      LLVMBuildICmp(ctx->builder, LLVMIntUGE, thread_id,
                    LLVMConstInt(ctx->i32, 2, 0), "tid_cmp");
   LLVMValueRef addend =
      LLVMBuildZExt(ctx->builder, tid_cmp, ctx->i32, "pos_y_addend");
   LLVMValueRef pos_y =
      LLVMBuildAdd(ctx->builder, quad_y_base, addend, "pos_y");
   return pos_y;
}

static void
get_pos(struct fsa_llvm_context *ctx, int component, LLVMValueRef *pos)
{
   LLVMValueRef vert_idx_arr_ptr =
      fsa_build_get_gfx_info_member_ptr(ctx, fsa_fsinfo_vert_idx, fsa_fs_info);
   LLVMValueRef vert_idx[3];
   for (int i = 0; i < 3; i++) {
      LLVMValueRef idx = LLVMConstInt(ctx->i64, i, 0);
      LLVMValueRef vert_idx_ptr = LLVMBuildGEP2(
         ctx->builder, ctx->i32, vert_idx_arr_ptr, &idx, 1, "vert_idx_ptr");
      vert_idx[i] =
         LLVMBuildLoad2(ctx->builder, ctx->i32, vert_idx_ptr, "vert_idx_i32");
      vert_idx[i] =
         LLVMBuildZExt(ctx->builder, vert_idx[i], ctx->i64, "vert_idx_i64");
   }
   /* Calc position address */
   LLVMValueRef pos_base =
      fsa_build_get_gfx_layout_base(ctx, fsa_vary_slot_pos, 0, fsa_fs_info);
   LLVMValueRef pos_stride =
      fsa_build_get_gfx_layout_stride(ctx, fsa_vary_slot_pos, 0, fsa_fs_info);
   LLVMValueRef v_addr[3], v_ptr[3], pos_ptr[3];
   for (int i = 0; i < 3; i++) {
      v_addr[i] = LLVMBuildAdd(ctx->builder, pos_base,
                               LLVMBuildMul(ctx->builder, vert_idx[i],
                                            pos_stride, "vidx_times_stride"),
                               "v_addr");
      v_ptr[i] = LLVMBuildIntToPtr(ctx->builder, v_addr[i], ctx->ptrt, "v_ptr");
      /* component xyzw <-> 0, 1, 2, 3 */
      LLVMValueRef offset = LLVMConstInt(ctx->i32, component, 0);
      pos_ptr[i] =
         LLVMBuildGEP2(ctx->builder, ctx->f32, v_ptr[i], &offset, 1, "pos_ptr");
      pos[i] = LLVMBuildLoad2(ctx->builder, ctx->f32, pos_ptr[i], "pos");
   }
}

static void
get_barycentric(struct fsa_llvm_context *ctx, LLVMValueRef *b)
{
   /* Barycentric coordinate is see as ana 1D array, we calculate the offset into
    * this array and get the pointer wtih GEP2 then finally load the value */
   LLVMValueRef bary_base_ptr = fsa_build_get_gfx_info_member_ptr(
      ctx, fsa_fsinfo_bary_coord, fsa_fs_info);
   LLVMValueRef tid = fsa_build_local_id(ctx);
   LLVMValueRef thread_offset = LLVMBuildMul(
      ctx->builder, tid, LLVMConstInt(ctx->i64, 3, false), "thread_offset");
   for (int i = 0; i < 3; i++) {
      /* comp_offset = offset for x, y, z */
      LLVMValueRef comp_offset = LLVMConstInt(ctx->i64, i, false);
      LLVMValueRef bary_offset =
         LLVMBuildAdd(ctx->builder, thread_offset, comp_offset, "bary_offset");
      LLVMValueRef bary_ptr =
         LLVMBuildGEP2(ctx->builder, ctx->f32, bary_base_ptr, &bary_offset, 1,
                       "final_bary_ptr");
      b[i] = LLVMBuildLoad2(ctx->builder, ctx->f32, bary_ptr, "bary");
   }
}

LLVMValueRef
fsa_build_load_frag_coord_z(struct fsa_llvm_context *ctx)
{
   /* Interoplate z = b0 * pos.z0 + b1 * pos.z1 + b2 * pos.z2
    * b is barycentric coord, pos is gl_Position */
   LLVMValueRef bary[3], pos[3];
   get_barycentric(ctx, bary);
   get_pos(ctx, 2, pos);
   LLVMValueRef mul[3];
   for (int i = 0; i < 3; i++) {
      mul[i] = LLVMBuildFMul(ctx->builder, bary[i], pos[i], "b_times_pos");
   }
   return LLVMBuildFAdd(ctx->builder, mul[2],
                        LLVMBuildFAdd(ctx->builder, mul[0], mul[1], ""), "");
}

LLVMValueRef
fsa_build_load_frag_coord_w(struct fsa_llvm_context *ctx)
{
   /* Interoplate w = b0 * pos.w0 + b1 * pos.w1 + b2 * pos.w2
    * b is barycentric coord, pos is gl_Position */
   LLVMValueRef bary[3], pos[3];
   get_barycentric(ctx, bary);
   get_pos(ctx, 3, pos);
   LLVMValueRef mul[3];
   for (int i = 0; i < 3; i++) {
      mul[i] = LLVMBuildFMul(ctx->builder, bary[i], pos[i], "b_times_pos");
   }
   return LLVMBuildFAdd(
      ctx->builder, mul[2],
      LLVMBuildFAdd(ctx->builder, mul[0], mul[1], "frag_coord_w_add"),
      "frag_coord_w_final");
}

void
fsa_build_submit_pixel(struct fsa_llvm_context *ctx, LLVMValueRef *params)
{
   LLVMValueRef tid = fsa_build_local_id(ctx);
   LLVMValueRef pix_mask_arr_ptr =
      fsa_build_get_gfx_info_member_ptr(ctx, fsa_fsinfo_pix_mask, fsa_fs_info);
   LLVMValueRef pix_mask_ptr = LLVMBuildGEP2(
      ctx->builder, ctx->i1, pix_mask_arr_ptr, &tid, 1, "pix_mask_ptr");
   LLVMValueRef pix_mask_val =
      LLVMBuildLoad2(ctx->builder, ctx->i1, pix_mask_ptr, "pix_mask_val");

   /* pix mask check */
   LLVMBasicBlockRef curr_block = LLVMGetInsertBlock(ctx->builder);
   LLVMValueRef func = LLVMGetBasicBlockParent(curr_block);
   LLVMBasicBlockRef then_block = LLVMAppendBasicBlock(func, "rop_block");
   LLVMBasicBlockRef merge_block =
      LLVMAppendBasicBlock(func, "rop_after_block");

   LLVMBuildCondBr(ctx->builder, pix_mask_val, then_block, merge_block);
   LLVMPositionBuilderAtEnd(ctx->builder, then_block);

   for (int i = 0; i < 3; i++) {
      /* arg1 depth value is float, bitcast to int and move on */
      params[i] = LLVMBuildBitCast(ctx->builder, params[i], ctx->i32, "");
      params[i] = LLVMBuildZExt(ctx->builder, params[i], ctx->i64, "");
   }
   fsa_build_intrinsic(ctx, "llvm.riscv.fsa.rop", ctx->voidt, params, 3, 0);
   LLVMBuildBr(ctx->builder, merge_block);
   LLVMPositionBuilderAtEnd(ctx->builder, merge_block);
}

LLVMValueRef
fsa_build_load_barycentric_pixel(struct fsa_llvm_context *ctx)
{
   LLVMValueRef bary[3] = {0};
   get_barycentric(ctx, bary);

   LLVMTypeRef v3_t = LLVMVectorType(ctx->f32, 3);
   LLVMValueRef v3 = LLVMGetUndef(v3_t);
   for (int i = 0; i < 3; i++) {
      LLVMValueRef idx = LLVMConstInt(ctx->i32, i, 0);
      v3 = LLVMBuildInsertElement(ctx->builder, v3, bary[i], idx, "");
   }
   return v3;
}

static unsigned
get_bit_size(LLVMTypeRef type)
{
   switch (LLVMGetTypeKind(type)) {
   case LLVMIntegerTypeKind:
      return LLVMGetIntTypeWidth(type);
   case LLVMFloatTypeKind:
      return 32;
   case LLVMHalfTypeKind:
      return 16;
   case LLVMDoubleTypeKind:
      return 64;
   default:
      assert("Unsupported type" && 0);
   }
}

/*
   Load flow
   1. Get layout (base + stride)
   2. Get global id
   3. base + id * stride
   4. Load val
*/
LLVMValueRef
fsa_build_load_input_gpu(struct fsa_llvm_context *ctx,
                         fsa_gfx_state_member member, int slot_idx,
                         LLVMValueRef input_idx, int component,
                         fsa_info_t info_type, LLVMTypeRef dest_type)
{
   LLVMValueRef base =
      fsa_build_get_gfx_layout_base(ctx, member, slot_idx, info_type);
   LLVMValueRef stride =
      fsa_build_get_gfx_layout_stride(ctx, member, slot_idx, info_type);
   /* ele base address = base + global_id * stride */
   LLVMValueRef addr_val = LLVMBuildAdd(
      ctx->builder,
      LLVMBuildMul(ctx->builder, stride, input_idx, "global_id_times_stride"),
      base, "base_addr");
   unsigned bit_size = get_bit_size(dest_type);
   assert("Only supports float now" && bit_size == 32);
   LLVMValueRef addr_offset =
      LLVMConstInt(ctx->i64, component * bit_size / 8, false);
   addr_val = LLVMBuildAdd(ctx->builder, addr_val, addr_offset,
                           "base_addr_plus_offset");
   /* Now that we get the addr, load the final scalar value */
   LLVMTypeRef dest_ptr_type = LLVMPointerType(dest_type, 0);
   LLVMValueRef ptr = LLVMBuildIntToPtr(ctx->builder, addr_val, dest_ptr_type,
                                        "load_input_ptr");
   return LLVMBuildLoad2(ctx->builder, dest_type, ptr, "load_input_val");
}

LLVMValueRef
fsa_get_vert_id(struct fsa_llvm_context *ctx, int idx)
{
   LLVMValueRef vert_id_arr_ptr =
      fsa_build_get_gfx_info_member_ptr(ctx, fsa_fsinfo_vert_idx, fsa_fs_info);
   LLVMValueRef idx_val = LLVMConstInt(ctx->i64, idx, 0);
   LLVMValueRef vert_id_ptr = LLVMBuildGEP2(
      ctx->builder, ctx->i32, vert_id_arr_ptr, &idx_val, 1, "vert_id_ptr");
   LLVMValueRef vert_id =
      LLVMBuildLoad2(ctx->builder, ctx->i32, vert_id_ptr, "vert_id");
   return LLVMBuildZExt(ctx->builder, vert_id, ctx->i64, "");
}

/* offset not used now */
LLVMValueRef
fsa_build_load_interpolated_input(struct fsa_llvm_context *ctx,
                                  LLVMValueRef bary, LLVMValueRef offset,
                                  fsa_gfx_state_member member, int slot_idx,
                                  int component, LLVMTypeRef dest_type)
{
   /* bary is a LLVM vec3, extract the value from it and interpolate the input */
   /* val = b0 * v0 + b1 * v1 + b2 * v2 */
   /* Load three vertices idx and corresponding inputs */
   LLVMValueRef vert_id[3], input[3], bary_scalar[3], mul_res[3];
   for (int i = 0; i < 3; i++) {
      LLVMValueRef idx = LLVMConstInt(ctx->i32, i, 0);
      bary_scalar[i] =
         LLVMBuildExtractElement(ctx->builder, bary, idx, "bary_scalar");
      vert_id[i] = fsa_get_vert_id(ctx, i);
      input[i] = fsa_build_load_input_gpu(ctx, member, slot_idx, vert_id[i],
                                          component, fsa_fs_info, dest_type);
      /* Hardcoded to f32, not interested to f64 now */
      input[i] = LLVMBuildBitCast(ctx->builder, input[i], ctx->f32, "");
      mul_res[i] =
         LLVMBuildFMul(ctx->builder, input[i], bary_scalar[i], "bary_mul_res");
   }
   /* Sum up */
   LLVMValueRef res = LLVMBuildFAdd(
      ctx->builder, mul_res[2],
      LLVMBuildFAdd(ctx->builder, mul_res[0], mul_res[1], "bary_add_mid"),
      "bary_add_final");
   return res;
}