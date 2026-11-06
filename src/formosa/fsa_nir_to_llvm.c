#include "fsa_nir_to_llvm.h"
#include "fsa_llvm_build.h"
#ifdef FSA_COMPUTE
#include "fsa_compute.h"
#endif

#include <stdbool.h>
#include "compiler/glsl_types.h"
#include "compiler/nir/nir.h"
#include "compiler/shader_info.h"
#include "util/hash_table.h"
#include <llvm-c/Core.h>

struct fsa_nir_context {
   struct fsa_llvm_context fsa;
   struct shader_info *info;
   gl_shader_stage stage;

   LLVMValueRef *ssa_defs;

   LLVMValueRef main_function;

   struct hash_table *func_map;
   struct hash_table *defs;
   struct hash_table *phis;
};

static LLVMTypeRef
get_def_type(struct fsa_nir_context *ctx, const nir_def *def)
{
   LLVMTypeRef type = LLVMIntTypeInContext(ctx->fsa.context, def->bit_size);
   if (def->num_components > 1) {
      type = LLVMVectorType(type, def->num_components);
   }
   return type;
}

static LLVMValueRef
get_src(struct fsa_nir_context *nir, nir_src src)
{
   return nir->ssa_defs[src.ssa->index];
}

static LLVMValueRef
get_alu_src(struct fsa_nir_context *ctx, nir_alu_src src,
            unsigned num_components)
{
   LLVMValueRef value = get_src(ctx, src.src);
   bool need_swizzle = false;

   assert(value);
   unsigned src_components = fsa_get_llvm_num_components(value);
   for (unsigned i = 0; i < num_components; ++i) {
      assert(src.swizzle[i] < src_components);
      if (src.swizzle[i] != i)
         need_swizzle = true;
   }

   if (need_swizzle || num_components != src_components) {
      LLVMValueRef masks[] = {
         LLVMConstInt(ctx->fsa.i32, src.swizzle[0], false),
         LLVMConstInt(ctx->fsa.i32, src.swizzle[1], false),
         LLVMConstInt(ctx->fsa.i32, src.swizzle[2], false),
         LLVMConstInt(ctx->fsa.i32, src.swizzle[3], false)};

      if (src_components > 1 && num_components == 1) {
         value = LLVMBuildExtractElement(ctx->fsa.builder, value, masks[0], "");
      } else if (src_components == 1 && num_components > 1) {
         LLVMValueRef values[] = {value, value, value, value};
         value = fsa_build_gather_values(&ctx->fsa, values, num_components);
      } else {
         LLVMValueRef swizzle = LLVMConstVector(masks, num_components);
         value =
            LLVMBuildShuffleVector(ctx->fsa.builder, value, value, swizzle, "");
      }
   }
   return value;
}

static LLVMValueRef
emit_int_cmp(struct fsa_llvm_context *ctx, LLVMIntPredicate pred,
             LLVMValueRef src0, LLVMValueRef src1)
{
   src0 = fsa_to_integer(ctx, src0);
   src1 = fsa_to_integer(ctx, src1);
   return LLVMBuildICmp(ctx->builder, pred, src0, src1, "");
}

static LLVMValueRef
emit_float_cmp(struct fsa_llvm_context *ctx, LLVMRealPredicate pred,
               LLVMValueRef src0, LLVMValueRef src1)
{
   src0 = fsa_to_float(ctx, src0);
   src1 = fsa_to_float(ctx, src1);
   return LLVMBuildFCmp(ctx->builder, pred, src0, src1, "");
}

static LLVMValueRef
emit_intrin_1f_param(struct fsa_llvm_context *ctx, const char *intrin,
                     LLVMTypeRef result_type, LLVMValueRef src0)
{
   char name[64], type[64];
   LLVMValueRef params[] = {
      fsa_to_float(ctx, src0),
   };

   fsa_build_type_name_for_intr(LLVMTypeOf(params[0]), type, sizeof(type));
   ASSERTED const int length =
      snprintf(name, sizeof(name), "%s.%s", intrin, type);
   assert(length < sizeof(name));
   return fsa_build_intrinsic(ctx, name, result_type, params, 1, 0);
}

static LLVMValueRef
emit_intrin_2f_param(struct fsa_llvm_context *ctx, const char *intrin,
                     LLVMTypeRef result_type, LLVMValueRef src0,
                     LLVMValueRef src1)
{
   char name[64], type[64];
   LLVMValueRef params[] = {
      fsa_to_float(ctx, src0),
      fsa_to_float(ctx, src1),
   };

   fsa_build_type_name_for_intr(LLVMTypeOf(params[0]), type, sizeof(type));
   ASSERTED const int length =
      snprintf(name, sizeof(name), "%s.%s", intrin, type);
   assert(length < sizeof(name));
   return fsa_build_intrinsic(ctx, name, result_type, params, 2, 0);
}

static LLVMValueRef
emit_intrin_3f_param(struct fsa_llvm_context *ctx, const char *intrin,
                     LLVMTypeRef result_type, LLVMValueRef src0,
                     LLVMValueRef src1, LLVMValueRef src2)
{
   char name[64], type[64];
   LLVMValueRef params[] = {
      fsa_to_float(ctx, src0),
      fsa_to_float(ctx, src1),
      fsa_to_float(ctx, src2),
   };

   fsa_build_type_name_for_intr(LLVMTypeOf(params[0]), type, sizeof(type));
   ASSERTED const int length =
      snprintf(name, sizeof(name), "%s.%s", intrin, type);
   assert(length < sizeof(name));
   return fsa_build_intrinsic(ctx, name, result_type, params, 3, 0);
}

static LLVMValueRef
emit_iabs(struct fsa_llvm_context *ctx, LLVMValueRef src0)
{
   return fsa_build_imax(ctx, src0, LLVMBuildNeg(ctx->builder, src0, ""));
}

static LLVMValueRef
emit_b2f(struct fsa_llvm_context *ctx, LLVMValueRef src0, unsigned bitsize)
{
   assert(fsa_get_elem_bits(ctx, LLVMTypeOf(src0)) == 1);

   switch (bitsize) {
   /* We dont need bitsize == 16, will be removed in the future */
   case 16:
      if (LLVMGetTypeKind(LLVMTypeOf(src0)) == LLVMVectorTypeKind) {
         assert(LLVMGetVectorSize(LLVMTypeOf(src0)) == 2);
         LLVMValueRef f[] = {
            LLVMBuildSelect(ctx->builder, fsa_llvm_extract_elem(ctx, src0, 0),
                            ctx->f16_1, ctx->f16_0, ""),
            LLVMBuildSelect(ctx->builder, fsa_llvm_extract_elem(ctx, src0, 1),
                            ctx->f16_1, ctx->f16_0, ""),
         };
         return fsa_build_gather_values(ctx, f, 2);
      }
      return LLVMBuildSelect(ctx->builder, src0, ctx->f16_1, ctx->f16_0, "");
   case 32:
      return LLVMBuildSelect(ctx->builder, src0, ctx->f32_1, ctx->f32_0, "");
   case 64:
      return LLVMBuildSelect(ctx->builder, src0, ctx->f64_1, ctx->f64_0, "");
   default:
      unreachable("Unsupported bit size.");
   }
}

static LLVMValueRef
emit_b2i(struct fsa_llvm_context *ctx, LLVMValueRef src0, unsigned bitsize)
{
   if (LLVMGetTypeKind(LLVMTypeOf(src0)) == LLVMVectorTypeKind) {
      LLVMValueRef tmp[4] = {0};
      unsigned vec_size = LLVMGetVectorSize(LLVMTypeOf(src0));
      for (unsigned i = 0; i < vec_size; i++) {
         switch (bitsize) {
         case 8:
            tmp[i] = LLVMBuildSelect(ctx->builder,
                                     fsa_llvm_extract_elem(ctx, src0, i),
                                     ctx->i8_1, ctx->i8_0, "");
            break;
         case 16:
            tmp[i] = LLVMBuildSelect(ctx->builder,
                                     fsa_llvm_extract_elem(ctx, src0, i),
                                     ctx->i16_1, ctx->i16_0, "");
            break;
         case 32:
            tmp[i] = LLVMBuildSelect(ctx->builder,
                                     fsa_llvm_extract_elem(ctx, src0, i),
                                     ctx->i32_1, ctx->i32_0, "");
            break;
         case 64:
            tmp[i] = LLVMBuildSelect(ctx->builder,
                                     fsa_llvm_extract_elem(ctx, src0, i),
                                     ctx->i64_1, ctx->i64_0, "");
            break;
         default:
            unreachable("Unsupported bit size.");
         }
      }
      return fsa_build_gather_values(ctx, tmp, vec_size);
   }

   switch (bitsize) {
   case 8:
      return LLVMBuildSelect(ctx->builder, src0, ctx->i8_1, ctx->i8_0, "");
   case 16:
      return LLVMBuildSelect(ctx->builder, src0, ctx->i16_1, ctx->i16_0, "");
   case 32:
      return LLVMBuildSelect(ctx->builder, src0, ctx->i32_1, ctx->i32_0, "");
   case 64:
      return LLVMBuildSelect(ctx->builder, src0, ctx->i64_1, ctx->i64_0, "");
   default:
      unreachable("Unsupported bit size.");
   }
}

static LLVMValueRef
emit_i2b(struct fsa_llvm_context *ctx, LLVMValueRef src0)
{
   LLVMValueRef zero = LLVMConstNull(LLVMTypeOf(src0));
   return LLVMBuildICmp(ctx->builder, LLVMIntNE, src0, zero, "");
}

static LLVMValueRef
emit_frsq(struct fsa_llvm_context *ctx, LLVMValueRef src, LLVMTypeRef type)
{
   LLVMValueRef result = emit_intrin_1f_param(ctx, "llvm.sqrt", type, src);
   return fsa_build_floating_reciprocal(ctx, result);
}

static LLVMValueRef
emit_bfm(struct fsa_llvm_context *ctx, LLVMValueRef bits, LLVMValueRef offset)
{
   /* mask = ((1 << bits) - 1) << offset */
   return LLVMBuildShl(
      ctx->builder,
      LLVMBuildSub(ctx->builder,
                   LLVMBuildShl(ctx->builder, ctx->i32_1, bits, ""), ctx->i32_1,
                   ""),
      offset, "");
}

static LLVMValueRef
emit_bitfield_select(struct fsa_llvm_context *ctx, LLVMValueRef mask,
                     LLVMValueRef insert, LLVMValueRef base)
{
   /* Calculate:
    *   (mask & insert) | (~mask & base) = base ^ (mask & (insert ^ base))
    * Use the right-hand side, which the LLVM backend can convert to V_BFI.
    */
   return LLVMBuildXor(
      ctx->builder, base,
      LLVMBuildAnd(ctx->builder, mask,
                   LLVMBuildXor(ctx->builder, insert, base, ""), ""),
      "");
}

static LLVMValueRef
emit_bcsel(struct fsa_llvm_context *ctx, LLVMValueRef src0, LLVMValueRef src1,
           LLVMValueRef src2)
{
   LLVMTypeRef src1_type = LLVMTypeOf(src1);
   LLVMTypeRef src2_type = LLVMTypeOf(src2);

   if (LLVMGetTypeKind(src1_type) == LLVMPointerTypeKind &&
       LLVMGetTypeKind(src2_type) != LLVMPointerTypeKind) {
      src2 = LLVMBuildIntToPtr(ctx->builder, src2, src1_type, "");
   } else if (LLVMGetTypeKind(src2_type) == LLVMPointerTypeKind &&
              LLVMGetTypeKind(src1_type) != LLVMPointerTypeKind) {
      src1 = LLVMBuildIntToPtr(ctx->builder, src1, src2_type, "");
   }

   return LLVMBuildSelect(ctx->builder, src0,
                          fsa_to_integer_or_pointer(ctx, src1),
                          fsa_to_integer_or_pointer(ctx, src2), "");
}

static LLVMValueRef
emit_uint_carry(struct fsa_llvm_context *ctx, const char *intrin,
                LLVMValueRef src0, LLVMValueRef src1)
{
   LLVMTypeRef ret_type;
   LLVMTypeRef types[] = {ctx->i32, ctx->i1};
   LLVMValueRef res;
   LLVMValueRef params[] = {src0, src1};
   ret_type = LLVMStructTypeInContext(ctx->context, types, 2, false);

   res = fsa_build_intrinsic(ctx, intrin, ret_type, params, 2, 0);

   res = LLVMBuildExtractValue(ctx->builder, res, 1, "");
   res = LLVMBuildZExt(ctx->builder, res, ctx->i32, "");
   return res;
}

static LLVMValueRef
emit_f2f16(struct fsa_llvm_context *ctx, LLVMValueRef src0)
{
   LLVMValueRef result;
   LLVMValueRef cond = NULL;

   src0 = fsa_to_float(ctx, src0);
   result = LLVMBuildFPTrunc(ctx->builder, src0, ctx->f16, "");

   /* need to convert back up to f32 */
   result = LLVMBuildFPExt(ctx->builder, result, ctx->f32, "");

   /* 0x38800000 is smallest half float value (2^-14) in 32-bit float,
    * so compare the result and flush to 0 if it's smaller.
    */
   LLVMValueRef temp, cond2;
   temp = emit_intrin_1f_param(ctx, "llvm.fabs", ctx->f32, result);
   cond = LLVMBuildFCmp(
      ctx->builder, LLVMRealOGT,
      LLVMBuildBitCast(ctx->builder, LLVMConstInt(ctx->i32, 0x38800000, false),
                       ctx->f32, ""),
      temp, "");
   cond2 = LLVMBuildFCmp(ctx->builder, LLVMRealONE, temp, ctx->f32_0, "");
   cond = LLVMBuildAnd(ctx->builder, cond, cond2, "");
   result = LLVMBuildSelect(ctx->builder, cond, ctx->f32_0, result, "");
   return result;
}

static LLVMValueRef
emit_umul_high(struct fsa_llvm_context *ctx, LLVMValueRef src0,
               LLVMValueRef src1)
{
   LLVMValueRef dst64, result;

   src0 = LLVMBuildZExt(ctx->builder, src0, ctx->i64, "");
   src1 = LLVMBuildZExt(ctx->builder, src1, ctx->i64, "");

   dst64 = LLVMBuildMul(ctx->builder, src0, src1, "");
   dst64 =
      LLVMBuildLShr(ctx->builder, dst64, LLVMConstInt(ctx->i64, 32, false), "");
   result = LLVMBuildTrunc(ctx->builder, dst64, ctx->i32, "");
   return result;
}

static LLVMValueRef
emit_imul_high(struct fsa_llvm_context *ctx, LLVMValueRef src0,
               LLVMValueRef src1)
{
   LLVMValueRef dst64, result;
   src0 = LLVMBuildSExt(ctx->builder, src0, ctx->i64, "");
   src1 = LLVMBuildSExt(ctx->builder, src1, ctx->i64, "");

   dst64 = LLVMBuildMul(ctx->builder, src0, src1, "");
   dst64 =
      LLVMBuildAShr(ctx->builder, dst64, LLVMConstInt(ctx->i64, 32, false), "");
   result = LLVMBuildTrunc(ctx->builder, dst64, ctx->i32, "");
   return result;
}

static LLVMValueRef
emit_mul24(struct fsa_llvm_context *ctx, LLVMValueRef src0, LLVMValueRef src1,
           bool sign)
{
   LLVMValueRef result, shamt = LLVMConstInt(ctx->i32, 8, false);
   src0 = LLVMBuildShl(ctx->builder, src0, shamt, "");
   src1 = LLVMBuildShl(ctx->builder, src1, shamt, "");
   if (sign) {
      src0 = LLVMBuildAShr(ctx->builder, src0, shamt, "");
      src1 = LLVMBuildAShr(ctx->builder, src1, shamt, "");
   } else {
      src0 = LLVMBuildLShr(ctx->builder, src0, shamt, "");
      src1 = LLVMBuildLShr(ctx->builder, src1, shamt, "");
   }
   result = LLVMBuildMul(ctx->builder, src0, src1, "");
   return result;
}

static void
nir_op_error(const nir_instr *instr, const char *msg)
{
   fprintf(stderr, "%s\n", msg);
   nir_print_instr(instr, stderr);
   fprintf(stderr, "\n");
}

static LLVMValueRef
emit_pack_2x16(struct fsa_llvm_context *ctx, LLVMValueRef src0,
               LLVMValueRef (*pack)(struct fsa_llvm_context *ctx,
                                    LLVMValueRef args[2]))
{
   LLVMValueRef comp[2];

   src0 = fsa_to_float(ctx, src0);
   comp[0] = LLVMBuildExtractElement(ctx->builder, src0, ctx->i32_0, "");
   comp[1] = LLVMBuildExtractElement(ctx->builder, src0, ctx->i32_1, "");

   return LLVMBuildBitCast(ctx->builder, pack(ctx, comp), ctx->i32, "");
}

static LLVMValueRef
emit_unpack_half_2x16(struct fsa_llvm_context *ctx, LLVMValueRef src0)
{
   LLVMValueRef const16 = LLVMConstInt(ctx->i32, 16, false);
   LLVMValueRef temps[2], val;
   int i;

   for (i = 0; i < 2; i++) {
      val = i == 1 ? LLVMBuildLShr(ctx->builder, src0, const16, "") : src0;
      val = LLVMBuildTrunc(ctx->builder, val, ctx->i16, "");
      val = LLVMBuildBitCast(ctx->builder, val, ctx->f16, "");
      temps[i] = LLVMBuildFPExt(ctx->builder, val, ctx->f32, "");
   }
   return fsa_build_gather_values(ctx, temps, 2);
}

static bool
visit_alu(struct fsa_nir_context *ctx, const nir_alu_instr *instr)
{
   LLVMValueRef src[16], result = NULL;
   LLVMTypeRef def_type = get_def_type(ctx, &instr->def);
   unsigned num_components = instr->def.num_components;
   for (int i = 0; i < nir_op_infos[instr->op].num_inputs; i++)
      src[i] = get_alu_src(ctx, instr->src[i],
                           nir_ssa_alu_instr_src_components(instr, i));

   switch (instr->op) {
   case nir_op_mov:
      result = src[0];
      break;
   case nir_op_fneg:
      src[0] = fsa_to_float(&ctx->fsa, src[0]);
      result = LLVMBuildFNeg(ctx->fsa.builder, src[0], "");
      break;
   case nir_op_inot:
      result = LLVMBuildNot(ctx->fsa.builder, src[0], "");
      break;
   case nir_op_ineg:
      result = LLVMBuildNeg(ctx->fsa.builder, src[0], "");
      break;
   case nir_op_iadd:
      result = LLVMBuildAdd(ctx->fsa.builder, src[0], src[1], "");
      break;
   /* Result will clamp to INT_MAX or INT_MIN, OpenGL does not have this
    * function. The build intrinsic part has not been verified. */
   case nir_op_uadd_sat:
   case nir_op_iadd_sat: {
      char name[64], type[64];
      fsa_build_type_name_for_intr(def_type, type, sizeof(type));
      snprintf(name, sizeof(name), "llvm.%cadd.sat.%s",
               instr->op == nir_op_uadd_sat ? 'u' : 's', type);
      result = fsa_build_intrinsic(&ctx->fsa, name, def_type, src, 2, 0);
      break;
   }
   case nir_op_usub_sat:
   case nir_op_isub_sat: {
      char name[64], type[64];
      fsa_build_type_name_for_intr(def_type, type, sizeof(type));
      snprintf(name, sizeof(name), "llvm.%csub.sat.%s",
               instr->op == nir_op_usub_sat ? 'u' : 's', type);
      result = fsa_build_intrinsic(&ctx->fsa, name, def_type, src, 2, 0);
      break;
   }
   case nir_op_fadd:
      src[0] = fsa_to_float(&ctx->fsa, src[0]);
      src[1] = fsa_to_float(&ctx->fsa, src[1]);
      result = LLVMBuildFAdd(ctx->fsa.builder, src[0], src[1], "");
      break;
   case nir_op_fsub:
      src[0] = fsa_to_float(&ctx->fsa, src[0]);
      src[1] = fsa_to_float(&ctx->fsa, src[1]);
      result = LLVMBuildFSub(ctx->fsa.builder, src[0], src[1], "");
      break;
   case nir_op_isub:
      result = LLVMBuildSub(ctx->fsa.builder, src[0], src[1], "");
      break;
   case nir_op_imul:
      result = LLVMBuildMul(ctx->fsa.builder, src[0], src[1], "");
      break;
   case nir_op_udiv:
      result = LLVMBuildUDiv(ctx->fsa.builder, src[0], src[1], "");
      break;
   case nir_op_idiv:
      result = LLVMBuildSDiv(ctx->fsa.builder, src[0], src[1], "");
      break;
   case nir_op_umod:
      result = LLVMBuildURem(ctx->fsa.builder, src[0], src[1], "");
      break;
   case nir_op_irem:
      result = LLVMBuildSRem(ctx->fsa.builder, src[0], src[1], "");
      break;
   case nir_op_imod: {
      LLVMValueRef rem = LLVMBuildSRem(ctx->fsa.builder, src[0], src[1], "");
      LLVMValueRef zero = LLVMConstNull(LLVMTypeOf(rem));
      LLVMValueRef opposite = LLVMBuildICmp(
         ctx->fsa.builder, LLVMIntSLT,
         LLVMBuildXor(ctx->fsa.builder, rem, src[1], ""), zero, "");
      LLVMValueRef nonzero =
         LLVMBuildICmp(ctx->fsa.builder, LLVMIntNE, rem, zero, "");
      result = LLVMBuildSelect(
         ctx->fsa.builder,
         LLVMBuildAnd(ctx->fsa.builder, opposite, nonzero, ""),
         LLVMBuildAdd(ctx->fsa.builder, rem, src[1], ""), rem, "");
      break;
   }
   case nir_op_imul24_relaxed:
      result = emit_mul24(&ctx->fsa, src[0], src[1], true);
      break;
   case nir_op_umul24_relaxed:
      result = emit_mul24(&ctx->fsa, src[0], src[1], false);
      break;
   case nir_op_fmul:
   case nir_op_fmulz: // If one of the result is zero, result is always +0
      src[0] = fsa_to_float(&ctx->fsa, src[0]);
      src[1] = fsa_to_float(&ctx->fsa, src[1]);
      result = LLVMBuildFMul(ctx->fsa.builder, src[0], src[1], "");
      break;
   case nir_op_frcp:
      src[0] = fsa_to_float(&ctx->fsa, src[0]);
      result = fsa_build_floating_reciprocal(&ctx->fsa, src[0]);
      break;
   case nir_op_iand:
      result = LLVMBuildAnd(ctx->fsa.builder, src[0], src[1], "");
      break;
   case nir_op_ior:
      result = LLVMBuildOr(ctx->fsa.builder, src[0], src[1], "");
      break;
   case nir_op_ixor:
      result = LLVMBuildXor(ctx->fsa.builder, src[0], src[1], "");
      break;
   case nir_op_ishl:
   case nir_op_ishr:
   case nir_op_ushr: {
      if (fsa_get_elem_bits(&ctx->fsa, LLVMTypeOf(src[1])) <
          fsa_get_elem_bits(&ctx->fsa, LLVMTypeOf(src[0])))
         src[1] =
            LLVMBuildZExt(ctx->fsa.builder, src[1], LLVMTypeOf(src[0]), "");
      else if (fsa_get_elem_bits(&ctx->fsa, LLVMTypeOf(src[1])) >
               fsa_get_elem_bits(&ctx->fsa, LLVMTypeOf(src[0])))
         src[1] =
            LLVMBuildTrunc(ctx->fsa.builder, src[1], LLVMTypeOf(src[0]), "");
      LLVMTypeRef type = LLVMTypeOf(src[1]);
      src[1] = LLVMBuildAnd(
         ctx->fsa.builder, src[1],
         fsa_build_const_int_vec(&ctx->fsa, type,
                                 fsa_get_elem_bits(&ctx->fsa, type) - 1, false),
         "");
      switch (instr->op) {
      case nir_op_ishl:
         result = LLVMBuildShl(ctx->fsa.builder, src[0], src[1], "");
         break;
      case nir_op_ishr:
         result = LLVMBuildAShr(ctx->fsa.builder, src[0], src[1], "");
         break;
      case nir_op_ushr:
         result = LLVMBuildLShr(ctx->fsa.builder, src[0], src[1], "");
         break;
      default:
         break;
      }
      break;
   }
   case nir_op_ilt:
      result = emit_int_cmp(&ctx->fsa, LLVMIntSLT, src[0], src[1]);
      break;
   case nir_op_ine:
      result = emit_int_cmp(&ctx->fsa, LLVMIntNE, src[0], src[1]);
      break;
   case nir_op_ieq:
      result = emit_int_cmp(&ctx->fsa, LLVMIntEQ, src[0], src[1]);
      break;
   case nir_op_ige:
      result = emit_int_cmp(&ctx->fsa, LLVMIntSGE, src[0], src[1]);
      break;
   case nir_op_ult:
      result = emit_int_cmp(&ctx->fsa, LLVMIntULT, src[0], src[1]);
      break;
   case nir_op_uge:
      result = emit_int_cmp(&ctx->fsa, LLVMIntUGE, src[0], src[1]);
      break;
   case nir_op_fdiv:
      src[0] = fsa_to_float(&ctx->fsa, src[0]);
      src[1] = fsa_to_float(&ctx->fsa, src[1]);
      result = LLVMBuildFDiv(ctx->fsa.builder, src[0], src[1], "");
      break;
   case nir_op_feq:
      result = emit_float_cmp(&ctx->fsa, LLVMRealOEQ, src[0], src[1]);
      break;
   case nir_op_fneu:
      result = emit_float_cmp(&ctx->fsa, LLVMRealUNE, src[0], src[1]);
      break;
   case nir_op_fequ:
      result = emit_float_cmp(&ctx->fsa, LLVMRealUEQ, src[0], src[1]);
      break;
   case nir_op_fneo:
      result = emit_float_cmp(&ctx->fsa, LLVMRealONE, src[0], src[1]);
      break;
   case nir_op_flt:
      result = emit_float_cmp(&ctx->fsa, LLVMRealOLT, src[0], src[1]);
      break;
   case nir_op_fge:
      result = emit_float_cmp(&ctx->fsa, LLVMRealOGE, src[0], src[1]);
      break;
   case nir_op_fltu:
      result = emit_float_cmp(&ctx->fsa, LLVMRealULT, src[0], src[1]);
      break;
   case nir_op_fgeu:
      result = emit_float_cmp(&ctx->fsa, LLVMRealUGE, src[0], src[1]);
      break;
   /* Check if the source can be ordered or not */
   case nir_op_funord:
      result = emit_float_cmp(&ctx->fsa, LLVMRealUNO, src[0], src[1]);
      break;
   case nir_op_ford:
      result = emit_float_cmp(&ctx->fsa, LLVMRealORD, src[0], src[1]);
      break;
   case nir_op_fabs:
      result =
         emit_intrin_1f_param(&ctx->fsa, "llvm.fabs",
                              fsa_to_float_type(&ctx->fsa, def_type), src[0]);
      break;
   case nir_op_fsat:
      src[0] = fsa_to_float(&ctx->fsa, src[0]);
      result = fsa_build_fsat(&ctx->fsa, src[0],
                              fsa_to_float_type(&ctx->fsa, def_type));
      break;
   case nir_op_iabs:
      result = emit_iabs(&ctx->fsa, src[0]);
      break;
   case nir_op_imax:
      result = fsa_build_imax(&ctx->fsa, src[0], src[1]);
      break;
   case nir_op_imin:
      result = fsa_build_imin(&ctx->fsa, src[0], src[1]);
      break;
   case nir_op_umax:
      result = fsa_build_umax(&ctx->fsa, src[0], src[1]);
      break;
   case nir_op_umin:
      result = fsa_build_umin(&ctx->fsa, src[0], src[1]);
      break;
   case nir_op_isign:
      result = fsa_build_isign(&ctx->fsa, src[0]);
      break;
   case nir_op_fsign:
      src[0] = fsa_to_float(&ctx->fsa, src[0]);
      result = fsa_build_fsign(&ctx->fsa, src[0]);
      break;
   case nir_op_ffloor:
      result =
         emit_intrin_1f_param(&ctx->fsa, "llvm.floor",
                              fsa_to_float_type(&ctx->fsa, def_type), src[0]);
      break;
   case nir_op_ftrunc:
      result =
         emit_intrin_1f_param(&ctx->fsa, "llvm.trunc",
                              fsa_to_float_type(&ctx->fsa, def_type), src[0]);
      break;
   case nir_op_fceil:
      result =
         emit_intrin_1f_param(&ctx->fsa, "llvm.ceil",
                              fsa_to_float_type(&ctx->fsa, def_type), src[0]);
      break;
   case nir_op_fround_even:
      result =
         emit_intrin_1f_param(&ctx->fsa, "llvm.roundeven",
                              fsa_to_float_type(&ctx->fsa, def_type), src[0]);
      break;
   case nir_op_ffract:
      src[0] = fsa_to_float(&ctx->fsa, src[0]);
      result = fsa_build_ffract(&ctx->fsa, src[0]);
      break;
   case nir_op_fsqrt:
      result =
         emit_intrin_1f_param(&ctx->fsa, "llvm.sqrt",
                              fsa_to_float_type(&ctx->fsa, def_type), src[0]);
      break;
   case nir_op_fsin:
   case nir_op_fcos:
      result = emit_intrin_1f_param(
         &ctx->fsa, instr->op == nir_op_fsin ? "llvm.sin" : "llvm.cos",
         fsa_to_float_type(&ctx->fsa, def_type), src[0]);
      break;
   case nir_op_fexp2:
      result =
         emit_intrin_1f_param(&ctx->fsa, "llvm.exp2",
                              fsa_to_float_type(&ctx->fsa, def_type), src[0]);
      break;
   case nir_op_flog2:
      result =
         emit_intrin_1f_param(&ctx->fsa, "llvm.log2",
                              fsa_to_float_type(&ctx->fsa, def_type), src[0]);
      break;
   case nir_op_frsq:
      src[0] = fsa_to_float(&ctx->fsa, src[0]);
      result =
         emit_frsq(&ctx->fsa, src[0], fsa_to_float_type(&ctx->fsa, def_type));
      break;
   case nir_op_frexp_exp:
      src[0] = fsa_to_float(&ctx->fsa, src[0]);
      result = fsa_build_frexp_exp(
         &ctx->fsa, src[0], fsa_get_elem_bits(&ctx->fsa, LLVMTypeOf(src[0])));
      if (fsa_get_elem_bits(&ctx->fsa, LLVMTypeOf(src[0])) == 16)
         result = LLVMBuildSExt(ctx->fsa.builder, result, ctx->fsa.i32, "");
      break;
   case nir_op_frexp_sig:
      src[0] = fsa_to_float(&ctx->fsa, src[0]);
      result = fsa_build_frexp_mant(&ctx->fsa, src[0], instr->def.bit_size);
      break;
   case nir_op_fmax:
      result = emit_intrin_2f_param(&ctx->fsa, "llvm.maxnum",
                                    fsa_to_float_type(&ctx->fsa, def_type),
                                    src[0], src[1]);
      break;
   case nir_op_fmin:
      result = emit_intrin_2f_param(&ctx->fsa, "llvm.minnum",
                                    fsa_to_float_type(&ctx->fsa, def_type),
                                    src[0], src[1]);
      break;
   case nir_op_ffma:
      result = emit_intrin_3f_param(&ctx->fsa, "llvm.fma",
                                    fsa_to_float_type(&ctx->fsa, def_type),
                                    src[0], src[1], src[2]);
      break;
   case nir_op_ffmaz:
      /* zero wins in mul, regardless of nan, inf, map to ffma for
       * simplification */
      result = emit_intrin_3f_param(&ctx->fsa, "llvm.fma",
                                    fsa_to_float_type(&ctx->fsa, def_type),
                                    src[0], src[1], src[2]);
      break;
   case nir_op_ldexp:
      src[0] = fsa_to_float(&ctx->fsa, src[0]);
      if (fsa_get_elem_bits(&ctx->fsa, def_type) == 32)
         result = fsa_build_intrinsic(&ctx->fsa, "llvm.ldexp.f32.i32",
                                      ctx->fsa.f32, src, 2, 0);
      else if (fsa_get_elem_bits(&ctx->fsa, def_type) == 16)
         result = fsa_build_intrinsic(&ctx->fsa, "llvm.ldexp.f16.i32",
                                      ctx->fsa.f16, src, 2, 0);
      else
         result = fsa_build_intrinsic(&ctx->fsa, "llvm.ldexp.f64.i32",
                                      ctx->fsa.f64, src, 2, 0);
      break;
   case nir_op_bfm:
      /* Bitfield move */
      result = emit_bfm(&ctx->fsa, src[0], src[1]);
      break;
   case nir_op_bitfield_select:
      result = emit_bitfield_select(&ctx->fsa, src[0], src[1], src[2]);
      break;
   case nir_op_ubfe:
      /* unsigned bitfield extract */
      result = fsa_build_bfe(&ctx->fsa, src[0], src[1], src[2], false);
      break;
   case nir_op_ibfe:
      result = fsa_build_bfe(&ctx->fsa, src[0], src[1], src[2], true);
      break;
   case nir_op_bitfield_reverse:
      result = fsa_build_bitfield_reverse(&ctx->fsa, src[0]);
      break;
   case nir_op_bit_count:
      result = fsa_build_bit_count(&ctx->fsa, src[0]);
      break;
   case nir_op_f2i8:
   case nir_op_f2i16:
   case nir_op_f2i32:
   case nir_op_f2i64:
      src[0] = fsa_to_float(&ctx->fsa, src[0]);
      result = LLVMBuildFPToSI(ctx->fsa.builder, src[0], def_type, "");
      break;
   case nir_op_f2u8:
   case nir_op_f2u16:
   case nir_op_f2u32:
   case nir_op_f2u64:
      src[0] = fsa_to_float(&ctx->fsa, src[0]);
      result = LLVMBuildFPToUI(ctx->fsa.builder, src[0], def_type, "");
      break;
   case nir_op_i2f16:
   case nir_op_i2f32:
   case nir_op_i2f64:
      result = LLVMBuildSIToFP(ctx->fsa.builder, src[0],
                               fsa_to_float_type(&ctx->fsa, def_type), "");
      break;
   case nir_op_u2f16:
   case nir_op_u2f32:
   case nir_op_u2f64:
      result = LLVMBuildUIToFP(ctx->fsa.builder, src[0],
                               fsa_to_float_type(&ctx->fsa, def_type), "");
      break;
   case nir_op_f2f16_rtz:
   case nir_op_f2f16:
   case nir_op_f2f16_rtne:
   case nir_op_f2f32:
   case nir_op_f2f64:
      src[0] = fsa_to_float(&ctx->fsa, src[0]);
      if (fsa_get_elem_bits(&ctx->fsa, LLVMTypeOf(src[0])) <
          fsa_get_elem_bits(&ctx->fsa, def_type))
         result = LLVMBuildFPExt(ctx->fsa.builder, src[0],
                                 fsa_to_float_type(&ctx->fsa, def_type), "");
      else
         result = LLVMBuildFPTrunc(ctx->fsa.builder, src[0],
                                   fsa_to_float_type(&ctx->fsa, def_type), "");
      break;
   case nir_op_u2u8:
   case nir_op_u2u16:
   case nir_op_u2u32:
   case nir_op_u2u64:
      if (fsa_get_elem_bits(&ctx->fsa, LLVMTypeOf(src[0])) <
          fsa_get_elem_bits(&ctx->fsa, def_type))
         result = LLVMBuildZExt(ctx->fsa.builder, src[0], def_type, "");
      else
         result = LLVMBuildTrunc(ctx->fsa.builder, src[0], def_type, "");
      break;
   case nir_op_i2i8:
   case nir_op_i2i16:
   case nir_op_i2i32:
   case nir_op_i2i64:
      if (fsa_get_elem_bits(&ctx->fsa, LLVMTypeOf(src[0])) <
          fsa_get_elem_bits(&ctx->fsa, def_type))
         result = LLVMBuildSExt(ctx->fsa.builder, src[0], def_type, "");
      else
         result = LLVMBuildTrunc(ctx->fsa.builder, src[0], def_type, "");
      break;
   case nir_op_bcsel:
      result = emit_bcsel(&ctx->fsa, src[0], src[1], src[2]);
      break;
   case nir_op_find_lsb:
      result = fsa_find_lsb(&ctx->fsa, ctx->fsa.i32, src[0]);
      break;
   case nir_op_ufind_msb:
      result = ac_build_umsb(&ctx->fsa, src[0], ctx->fsa.i32, false);
      break;
   case nir_op_ifind_msb:
      result = fsa_build_imsb(&ctx->fsa, src[0], ctx->fsa.i32, false);
      break;
   case nir_op_ufind_msb_rev:
      result = ac_build_umsb(&ctx->fsa, src[0], ctx->fsa.i32, true);
      break;
   case nir_op_ifind_msb_rev:
      result = fsa_build_imsb(&ctx->fsa, src[0], ctx->fsa.i32, true);
      break;
   case nir_op_uclz: {
      LLVMValueRef params[2] = {
         src[0],
         ctx->fsa.i1false,
      };
      result = fsa_build_intrinsic(&ctx->fsa, "llvm.ctlz.i32", ctx->fsa.i32,
                                   params, 2, 0);
      break;
   }
   case nir_op_uadd_carry:
      result = emit_uint_carry(&ctx->fsa, "llvm.uadd.with.overflow.i32", src[0],
                               src[1]);
      break;
   case nir_op_usub_borrow:
      result = emit_uint_carry(&ctx->fsa, "llvm.usub.with.overflow.i32", src[0],
                               src[1]);
      break;
   case nir_op_b2f16:
   case nir_op_b2f32:
   case nir_op_b2f64:
      result = emit_b2f(&ctx->fsa, src[0], instr->def.bit_size);
      break;
   case nir_op_b2i8:
   case nir_op_b2i16:
   case nir_op_b2i32:
   case nir_op_b2i64:
      result = emit_b2i(&ctx->fsa, src[0], instr->def.bit_size);
      break;
   case nir_op_b2b1: /* after loads */
      result = emit_i2b(&ctx->fsa, src[0]);
      break;
   case nir_op_b2b16: /* before stores */
      result = LLVMBuildZExt(ctx->fsa.builder, src[0], ctx->fsa.i16, "");
      break;
   case nir_op_b2b32: /* before stores */
      result = LLVMBuildZExt(ctx->fsa.builder, src[0], ctx->fsa.i32, "");
      break;
   case nir_op_fquantize2f16:
      result = emit_f2f16(&ctx->fsa, src[0]);
      break;
   case nir_op_umul_high:
      result = emit_umul_high(&ctx->fsa, src[0], src[1]);
      break;
   case nir_op_imul_high:
      result = emit_imul_high(&ctx->fsa, src[0], src[1]);
      break;
   case nir_op_pack_half_2x16_rtz_split:
   case nir_op_pack_half_2x16_split:
      nir_op_error(&instr->instr, "GLSL packHalf2x16 only supported in 4.0 up");
      break;
   case nir_op_pack_snorm_2x16:
   case nir_op_pack_unorm_2x16: {
      unsigned bit_size = instr->src[0].src.ssa->bit_size;
      /* Only support 16 and 32bit. */
      assert(bit_size == 16 || bit_size == 32);

      LLVMValueRef data = src[0];
      LLVMValueRef (*pack)(struct fsa_llvm_context *ctx, LLVMValueRef args[2]);
      if (bit_size == 32) {
         pack = instr->op == nir_op_pack_snorm_2x16 ? fsa_build_cvt_pknorm_i16
                                                    : fsa_build_cvt_pknorm_u16;
      } else {
         pack = instr->op == nir_op_pack_snorm_2x16
                   ? fsa_build_cvt_pknorm_i16_f16
                   : fsa_build_cvt_pknorm_u16_f16;
      }
      result = emit_pack_2x16(&ctx->fsa, data, pack);
      break;
   }
   case nir_op_pack_uint_2x16: {
      LLVMValueRef comp[2];
      comp[0] =
         LLVMBuildExtractElement(ctx->fsa.builder, src[0], ctx->fsa.i32_0, "");
      comp[1] =
         LLVMBuildExtractElement(ctx->fsa.builder, src[0], ctx->fsa.i32_1, "");

      result = fsa_build_pack_uint_2x16(&ctx->fsa, comp);
      break;
   }
   case nir_op_pack_sint_2x16: {
      LLVMValueRef comp[2];
      comp[0] =
         LLVMBuildExtractElement(ctx->fsa.builder, src[0], ctx->fsa.i32_0, "");
      comp[1] =
         LLVMBuildExtractElement(ctx->fsa.builder, src[0], ctx->fsa.i32_1, "");

      result = fsa_build_pack_sint_2x16(&ctx->fsa, comp);
      break;
   }
   case nir_op_unpack_half_2x16_split_x: {
      assert(fsa_get_llvm_num_components(src[0]) == 1);
      LLVMValueRef tmp = emit_unpack_half_2x16(&ctx->fsa, src[0]);
      result =
         LLVMBuildExtractElement(ctx->fsa.builder, tmp, ctx->fsa.i32_0, "");
      break;
   }
   case nir_op_unpack_half_2x16_split_y: {
      assert(fsa_get_llvm_num_components(src[0]) == 1);
      LLVMValueRef tmp = emit_unpack_half_2x16(&ctx->fsa, src[0]);
      result =
         LLVMBuildExtractElement(ctx->fsa.builder, tmp, ctx->fsa.i32_1, "");
      break;
   }
   case nir_op_unpack_64_4x16: {
      result = LLVMBuildBitCast(ctx->fsa.builder, src[0], ctx->fsa.v4i16, "");
      break;
   }

   case nir_op_unpack_64_2x32: {
      result = LLVMBuildBitCast(ctx->fsa.builder, src[0], ctx->fsa.v2i32, "");
      break;
   }
   case nir_op_unpack_64_2x32_split_x: {
      assert(fsa_get_llvm_num_components(src[0]) == 1);
      LLVMValueRef tmp =
         LLVMBuildBitCast(ctx->fsa.builder, src[0], ctx->fsa.v2i32, "");
      result =
         LLVMBuildExtractElement(ctx->fsa.builder, tmp, ctx->fsa.i32_0, "");
      break;
   }
   case nir_op_unpack_64_2x32_split_y: {
      assert(fsa_get_llvm_num_components(src[0]) == 1);
      LLVMValueRef tmp =
         LLVMBuildBitCast(ctx->fsa.builder, src[0], ctx->fsa.v2i32, "");
      result =
         LLVMBuildExtractElement(ctx->fsa.builder, tmp, ctx->fsa.i32_1, "");
      break;
   }

   case nir_op_pack_64_2x32:
      result = LLVMBuildBitCast(ctx->fsa.builder, src[0], ctx->fsa.i64, "");
      break;
   case nir_op_pack_64_2x32_split: {
      LLVMValueRef tmp = fsa_build_gather_values(&ctx->fsa, src, 2);
      result = LLVMBuildBitCast(ctx->fsa.builder, tmp, ctx->fsa.i64, "");
      break;
   }

   case nir_op_pack_32_4x8: {
      result = LLVMBuildBitCast(ctx->fsa.builder, src[0], ctx->fsa.i32, "");
      break;
   }
   case nir_op_pack_32_2x16_split: {
      LLVMValueRef tmp = fsa_build_gather_values(&ctx->fsa, src, 2);
      result = LLVMBuildBitCast(ctx->fsa.builder, tmp, ctx->fsa.i32, "");
      break;
   }

   case nir_op_unpack_32_4x8:
      result = LLVMBuildBitCast(ctx->fsa.builder, src[0], ctx->fsa.v4i8, "");
      break;
   case nir_op_unpack_32_2x16: {
      result = LLVMBuildBitCast(ctx->fsa.builder, src[0], ctx->fsa.v2i16, "");
      break;
   }
   case nir_op_unpack_32_2x16_split_x: {
      LLVMValueRef tmp =
         LLVMBuildBitCast(ctx->fsa.builder, src[0], ctx->fsa.v2i16, "");
      result =
         LLVMBuildExtractElement(ctx->fsa.builder, tmp, ctx->fsa.i32_0, "");
      break;
   }
   case nir_op_unpack_32_2x16_split_y: {
      LLVMValueRef tmp =
         LLVMBuildBitCast(ctx->fsa.builder, src[0], ctx->fsa.v2i16, "");
      result =
         LLVMBuildExtractElement(ctx->fsa.builder, tmp, ctx->fsa.i32_1, "");
      break;
   }
   case nir_op_extract_u8:
   case nir_op_extract_i8:
   case nir_op_extract_u16:
   case nir_op_extract_i16: {
      bool is_signed =
         instr->op == nir_op_extract_i16 || instr->op == nir_op_extract_i8;
      unsigned size =
         instr->op == nir_op_extract_u8 || instr->op == nir_op_extract_i8 ? 8
                                                                          : 16;
      LLVMValueRef offset = LLVMConstInt(
         LLVMTypeOf(src[0]), nir_src_as_uint(instr->src[1].src) * size, false);
      result = LLVMBuildLShr(ctx->fsa.builder, src[0], offset, "");
      result = LLVMBuildTrunc(ctx->fsa.builder, result,
                              LLVMIntTypeInContext(ctx->fsa.context, size), "");
      if (is_signed)
         result =
            LLVMBuildSExt(ctx->fsa.builder, result, LLVMTypeOf(src[0]), "");
      else
         result =
            LLVMBuildZExt(ctx->fsa.builder, result, LLVMTypeOf(src[0]), "");
      break;
   }

   case nir_op_insert_u8:
   case nir_op_insert_u16: {
      unsigned size = instr->op == nir_op_insert_u8 ? 8 : 16;
      LLVMValueRef offset = LLVMConstInt(
         LLVMTypeOf(src[0]), nir_src_as_uint(instr->src[1].src) * size, false);
      LLVMValueRef mask =
         LLVMConstInt(LLVMTypeOf(src[0]), u_bit_consecutive(0, size), false);
      result = LLVMBuildShl(ctx->fsa.builder,
                            LLVMBuildAnd(ctx->fsa.builder, src[0], mask, ""),
                            offset, "");
      break;
   }
   case nir_op_sdot_4x8_iadd:
      result = fsa_build_sdot_4x8_iadd(&ctx->fsa, src);
      break;
   case nir_op_sdot_4x8_iadd_sat:
      result = fsa_build_sdot_4x8_iadd_sat(&ctx->fsa, src);
      break;
   case nir_op_sudot_4x8_iadd:
      result = fsa_build_sudot_4x8_iadd(&ctx->fsa, src);
      break;
   case nir_op_sudot_4x8_iadd_sat:
      result = fsa_build_sudot_4x8_iadd_sat(&ctx->fsa, src);
      break;
   case nir_op_udot_4x8_uadd:
      result = fsa_build_udot_4x8_uadd(&ctx->fsa, src);
      break;
   case nir_op_udot_4x8_uadd_sat:
      result = fsa_build_udot_4x8_uadd_sat(&ctx->fsa, src);
      break;
   case nir_op_sdot_2x16_iadd:
      result = fsa_build_sdot_2x16_iadd(&ctx->fsa, src);
      break;
   case nir_op_udot_2x16_uadd:
      result = fsa_build_udot_2x16_uadd(&ctx->fsa, src);
      break;
   case nir_op_sdot_2x16_iadd_sat:
      result = fsa_build_sdot_2x16_iadd_sat(&ctx->fsa, src);
      break;
   case nir_op_udot_2x16_uadd_sat:
      result = fsa_build_udot_2x16_uadd_sat(&ctx->fsa, src);
      break;
   case nir_op_msad_4x8:
      result = fsa_build_msad_4x8(&ctx->fsa, src);
      break;
   case nir_op_mqsad_4x8:
      result = fsa_build_mqsad_4x8(&ctx->fsa, src);
      break;
   case nir_op_shfr:
      result =
         fsa_build_intrinsic(&ctx->fsa, "llvm.fshr.i32", ctx->fsa.i32,
                             (LLVMValueRef[]){src[0], src[1], src[2]}, 3, 0);
      break;
   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4:
   case nir_op_vec5:
   case nir_op_vec8:
   case nir_op_vec16:
      result = fsa_build_gather_values(&ctx->fsa, src, num_components);
      break;
   default:
      fprintf(stderr, "Unknown NIR alu instr: ");
      nir_print_instr(&instr->instr, stderr);
      fprintf(stderr, "\n");
      return false;
   }
   if (result) {
      LLVMTypeKind type_kind = LLVMGetTypeKind(LLVMTypeOf(result));
      bool is_float = type_kind == LLVMHalfTypeKind ||
                      type_kind == LLVMFloatTypeKind ||
                      type_kind == LLVMDoubleTypeKind;
      if (is_float)
         result =
            fsa_build_canonicalize(&ctx->fsa, result, instr->def.bit_size);

      result = fsa_to_integer_or_pointer(&ctx->fsa, result);
      ctx->ssa_defs[instr->def.index] = result;
   }
   return true;
}

static int load_count = 0;
static int store_count = 0;

/* Used for host (LLVMMCJit and spike) debug */
static LLVMValueRef
visit_load_input_cpu(struct fsa_nir_context *ctx, nir_intrinsic_instr *instr)
{
   assert(instr->def.num_components == 1);
   LLVMTypeRef dest_type = get_def_type(ctx, &instr->def);
   /*
      base = which slot , e.g. layout(location = 5) -> base = 5
      component = which component to start, .y -> component = 1
      count = how many comps to read, .yz -> count = 2
      offset = array index, there is type like in vec3[4] aPos
   */
   unsigned component = nir_intrinsic_component(instr);

   uint64_t addr = 0x87650000 + component * 4 + (load_count++ << 8);

   LLVMValueRef result = LLVMGetUndef(dest_type);
   LLVMValueRef input_addr = LLVMConstInt(ctx->fsa.i64, addr, 0);
   return fsa_build_load_input_to_scalar(&ctx->fsa, input_addr, result);
}

/*
   1. Get layout (base + stride)
   2. Get global id
   3. base + id * stride
   4. Load val
*/
static LLVMValueRef
visit_load_input_vs(struct fsa_nir_context *ctx, nir_intrinsic_instr *instr)
{
   /* fsa only deals with scalar io */
   assert(instr->def.num_components == 1);
   /* Calculate offset into gfx_state based on slot */
   nir_io_semantics ios = nir_intrinsic_io_semantics(instr);
   gl_vert_attrib attr = (gl_vert_attrib)ios.location;
   if (attr < VERT_ATTRIB_GENERIC0 || attr > VERT_ATTRIB_GENERIC15) {
      printf("Invalid vert attrib slot: %d\n", attr);
      exit(-1);
   }
   fsa_gfx_state_member member = fsa_vert_attrib;
   int idx = attr - VERT_ATTRIB_GENERIC0;
   LLVMValueRef input_idx = fsa_build_global_id(&ctx->fsa, fsa_vs_info);
   int component = nir_intrinsic_component(instr);
   fsa_info_t info_type = fsa_vs_info;
   LLVMTypeRef dest_type = get_def_type(ctx, &instr->def);
   return fsa_build_load_input_gpu(&ctx->fsa, member, idx, input_idx, component,
                                   info_type, dest_type);
}

static LLVMValueRef
visit_load_input_fs(struct fsa_nir_context *ctx, nir_intrinsic_instr *instr)
{
   assert(instr->def.num_components == 1);
   /* Calculate offset into gfx_state based on slot */
   fsa_gfx_state_member member;
   unsigned idx = 0;
   nir_io_semantics ios = nir_intrinsic_io_semantics(instr);
   gl_varying_slot slot = (gl_varying_slot)ios.location;
   if (slot == VARYING_SLOT_POS) {
      member = fsa_vary_slot_pos;
   } else if (slot >= VARYING_SLOT_VAR0 && slot <= VARYING_SLOT_VAR31) {
      member = fsa_vary_slot_var;
      idx = slot - VARYING_SLOT_VAR0;
   } else {
      printf("Unsupported FS input slot\n");
      exit(-1);
   }
   /* Flat input will take the last vertex's value */
   LLVMValueRef input_idx = fsa_get_vert_id(&ctx->fsa, 2);
   int component = nir_intrinsic_component(instr);
   fsa_info_t info_type = fsa_fs_info;
   LLVMTypeRef dest_type = get_def_type(ctx, &instr->def);
   return fsa_build_load_input_gpu(&ctx->fsa, member, idx, input_idx, component,
                                   info_type, dest_type);
}

/* fsa only deals with scalar io */
static void
visit_store_output_cpu(struct fsa_nir_context *ctx, nir_intrinsic_instr *instr)
{
   /* store_output val=src[0] no def */
   assert(instr->src[0].ssa->num_components == 1);
   unsigned component = nir_intrinsic_component(instr);
   LLVMValueRef src = fsa_to_float(&ctx->fsa, get_src(ctx, instr->src[0]));

   LLVMTypeRef elem_type = LLVMTypeOf(src);
   /*
      base = which slot
      writemask start at bit 0
      component = which component to start
      e.g. out_var.yz = vec2(val1, val2)
      writemask = 0011, component = 1
      writemask <<= 1 -> 0110
   */

   uint64_t addr = 0x12340000 + component * 4 + (store_count++ << 8);
   /* Fake output address, TODO */
   LLVMValueRef output_addr = LLVMConstInt(ctx->fsa.i64, addr, 0);
   LLVMValueRef output_ptr =
      fsa_get_pointer_val(&ctx->fsa, elem_type, output_addr);
   LLVMBuildStore(ctx->fsa.builder, src, output_ptr);
}

/*
   1. Get layout (base + stride)
   2. Get global id
   3. base + id * stride
   4. Load val
*/
static void
visit_store_output_gpu_trivial(struct fsa_nir_context *ctx,
                               nir_intrinsic_instr *instr)
{
   /* fsa only deals with scalar io */
   /* store_output val=src[0] no def */
   assert(instr->src[0].ssa->num_components == 1);
   /* Calculate offset into gfx_state based on slot */
   fsa_gfx_state_member member;
   unsigned idx = 0;
   nir_io_semantics ios = nir_intrinsic_io_semantics(instr);
   if (ctx->stage == MESA_SHADER_VERTEX) {
      gl_varying_slot slot = (gl_varying_slot)ios.location;
      if (slot == VARYING_SLOT_POS) {
         member = fsa_vary_slot_pos;
      } else if (slot == VARYING_SLOT_PSIZ) {
         member = fsa_vary_slot_psize;
      } else if (slot >= VARYING_SLOT_VAR0 && slot <= VARYING_SLOT_VAR31) {
         member = fsa_vary_slot_var;
         idx = slot - VARYING_SLOT_VAR0;
      } else {
         printf("Unsupported VS output slot\n");
         exit(-1);
      }
   } else {
      /* Write to color, depth and stencil must be stored in special register
       * ready to commit at and of FS. Writing to other output slots will be
       * lowered to memory load/store */
      gl_frag_result res = (gl_frag_result)ios.location;
      if (res >= FRAG_RESULT_DATA0 && res <= FRAG_RESULT_DATA7) {
         member = fsa_frag_result;
         idx = res - FRAG_RESULT_DATA0;
      } else {
         printf("Unsupported FS output slot\n");
         exit(-1);
      }
   }
   fsa_info_t info_type =
      ctx->stage == MESA_SHADER_VERTEX ? fsa_vs_info : fsa_fs_info;
   /* Addr calc is the same as load */
   LLVMValueRef base =
      fsa_build_get_gfx_layout_base(&ctx->fsa, member, idx, info_type);
   LLVMValueRef stride =
      fsa_build_get_gfx_layout_stride(&ctx->fsa, member, idx, info_type);
   LLVMValueRef global_id = fsa_build_global_id(&ctx->fsa, info_type);
   LLVMValueRef addr_val = LLVMBuildAdd(
      ctx->fsa.builder, LLVMBuildMul(ctx->fsa.builder, stride, global_id, ""),
      base, "");
   int component = nir_intrinsic_component(instr);
   unsigned bit_size =
      fsa_get_elem_bits(&ctx->fsa, LLVMTypeOf(get_src(ctx, instr->src[0])));
   assert("Only supports float now" && bit_size == 32);
   LLVMValueRef addr_offset =
      LLVMConstInt(ctx->fsa.i64, component * bit_size / 8, false);
   addr_val = LLVMBuildAdd(ctx->fsa.builder, addr_val, addr_offset, "");

   /* TODO: Handle different data type */
   LLVMValueRef src = fsa_to_float(&ctx->fsa, get_src(ctx, instr->src[0]));
   LLVMTypeRef elem_type = LLVMTypeOf(src);
   LLVMValueRef output_ptr =
      fsa_get_pointer_val(&ctx->fsa, elem_type, addr_val);
   LLVMBuildStore(ctx->fsa.builder, src, output_ptr);
}

static LLVMValueRef
visit_load_interpolated_input(struct fsa_nir_context *ctx,
                              nir_intrinsic_instr *instr)
{
   /* Offset is not supported now  */
   assert(ctx->stage == MESA_SHADER_FRAGMENT);
   assert(instr->def.num_components == 1);
   LLVMValueRef bary = get_src(ctx, instr->src[0]);
   unsigned idx = 0;
   fsa_gfx_state_member member;
   int component = nir_intrinsic_component(instr);
   nir_io_semantics ios = nir_intrinsic_io_semantics(instr);
   gl_varying_slot slot = (gl_varying_slot)ios.location;
   if (slot == VARYING_SLOT_POS) {
      member = fsa_vary_slot_pos;
   } else if (slot >= VARYING_SLOT_VAR0 && slot <= VARYING_SLOT_VAR31) {
      member = fsa_vary_slot_var;
      idx = slot - VARYING_SLOT_VAR0;
   } else {
      printf("Unsupported FS input slot\n");
      exit(-1);
   }
   LLVMTypeRef dest_type = get_def_type(ctx, &instr->def);
   return fsa_build_load_interpolated_input(&ctx->fsa, bary, NULL, member, idx,
                                            component, dest_type);
}

static LLVMValueRef
visit_load_ubo(struct fsa_nir_context *ctx, nir_intrinsic_instr *instr)
{
   assert(instr->src[0].ssa->num_components == 1);
   assert(nir_src_is_const(instr->src[0]));
   assert(nir_src_is_const(instr->src[1]));

   uint64_t idx = nir_src_as_uint(instr->src[0]);
   uint64_t offset = nir_src_as_uint(instr->src[1]);

   if (idx == 16) {
      return fsa_build_get_viewport_scale(&ctx->fsa, offset);
   } else if (idx == 17) {
      return fsa_build_get_viewport_offset(&ctx->fsa, offset);
   }
   fsa_info_t info_type =
      ctx->stage == MESA_SHADER_VERTEX ? fsa_vs_info : fsa_fs_info;
   LLVMTypeRef dest_type = get_def_type(ctx, &instr->def);

   LLVMValueRef base_ptr = fsa_build_get_gfx_state_arr_member_ptr(
      &ctx->fsa, fsa_ubo, ctx->fsa.i64, idx, info_type);
   LLVMValueRef base =
      LLVMBuildLoad2(ctx->fsa.builder, ctx->fsa.i64, base_ptr, "ubo_base");
   LLVMValueRef offset_val = LLVMConstInt(ctx->fsa.i64, offset, false);
   LLVMValueRef addr =
      LLVMBuildAdd(ctx->fsa.builder, base, offset_val, "ubo_addr");
   LLVMValueRef ptr =
      LLVMBuildIntToPtr(ctx->fsa.builder, addr, ctx->fsa.ptrt, "ubo_ptr");
   return LLVMBuildLoad2(ctx->fsa.builder, dest_type, ptr, "ubo_val");
}

static bool
visit_intrinsic(struct fsa_nir_context *ctx, nir_intrinsic_instr *instr)
{
   LLVMValueRef result = NULL;
#ifdef FSA_COMPUTE
   if (ctx->stage == MESA_SHADER_COMPUTE) {
      LLVMValueRef src[8] = {0};
      unsigned count = nir_intrinsic_infos[instr->intrinsic].num_srcs;
      if (count > ARRAY_SIZE(src))
         return false;
      for (unsigned i = 0; i < count; i++)
         src[i] = get_src(ctx, instr->src[i]);
      if (!fsa_compute_emit_intrinsic(&ctx->fsa, instr, src, &result))
         return false;
      if (result)
         ctx->ssa_defs[instr->def.index] = result;
      return true;
   }
#endif
   switch (instr->intrinsic) {
   case nir_intrinsic_load_input:
      if (ctx->fsa.is_cpu) {
         result = visit_load_input_cpu(ctx, instr);
      } else if (ctx->stage == MESA_SHADER_VERTEX) {
         result = visit_load_input_vs(ctx, instr);
      } else if (ctx->stage == MESA_SHADER_FRAGMENT) {
         result = visit_load_input_fs(ctx, instr);
      } else {
         assert("Unsupported stage" && 0);
      }
      break;
   case nir_intrinsic_store_output:
      if (ctx->fsa.is_cpu)
         visit_store_output_cpu(ctx, instr);
      else
         visit_store_output_gpu_trivial(ctx, instr);
      break;
   case nir_intrinsic_load_uniform:
      assert("Should be lowered to load_ubo" && 0);
      break;
   case nir_intrinsic_load_ubo:
      result = visit_load_ubo(ctx, instr);
      break;

   case nir_intrinsic_load_frag_coord_x_fsa:
      result = fsa_build_load_frag_coord_x(&ctx->fsa);
      break;
   case nir_intrinsic_load_frag_coord_y_fsa:
      result = fsa_build_load_frag_coord_y(&ctx->fsa);
      break;
   case nir_intrinsic_load_frag_coord_z_fsa:
      result = fsa_build_load_frag_coord_z(&ctx->fsa);
      break;
   case nir_intrinsic_load_frag_coord_w_fsa:
      result = fsa_build_load_frag_coord_w(&ctx->fsa);
      break;

   case nir_intrinsic_submit_pixel_fsa: {
      LLVMValueRef src[3] = {get_src(ctx, instr->src[0]),
                             get_src(ctx, instr->src[1]),
                             get_src(ctx, instr->src[2])};
      fsa_build_submit_pixel(&ctx->fsa, src);
      break;
   }
   case nir_intrinsic_load_barycentric_pixel:
      result = fsa_build_load_barycentric_pixel(&ctx->fsa);
      break;
   case nir_intrinsic_load_interpolated_input:
      result = visit_load_interpolated_input(ctx, instr);
      break;

   case nir_intrinsic_quad_swap_horizontal:
      result = fsa_build_quad_swap(&ctx->fsa, get_src(ctx, instr->src[0]), 0);
      break;
   case nir_intrinsic_quad_swap_vertical:
      result = fsa_build_quad_swap(&ctx->fsa, get_src(ctx, instr->src[0]), 1);
      break;
   case nir_intrinsic_load_subgroup_invocation:
      result = fsa_build_lane_id(&ctx->fsa);
      /* xlaneid returns 64-bit, truncate to 32 for later use */
      result = LLVMBuildTrunc(ctx->fsa.builder, result, ctx->fsa.i32, "");
      break;
   default:
      printf("[V_INTRINISC] Type: %d not implemented,Instr: ",
             instr->instr.type);
      nir_print_instr(&instr->instr, stdout);
      printf("\n");
      exit(-1);
   }

   if (result) {
      ctx->ssa_defs[instr->def.index] = result;
   }
   return true;
}

static unsigned
get_tex_idx(nir_tex_instr *instr)
{
   unsigned tex_unit = instr->texture_index;
   int offset_idx = nir_tex_instr_src_index(instr, nir_tex_src_texture_offset);

   /* No support for texture array */
   assert(offset_idx == -1);
   return tex_unit;
}

static bool
visit_tex(struct fsa_nir_context *ctx, nir_tex_instr *instr)
{
   LLVMValueRef result = NULL;
   switch (instr->op) {
   case nir_texop_txs: {
      fsa_info_t info =
         ctx->stage == MESA_SHADER_VERTEX ? fsa_vs_info : fsa_fs_info;
      LLVMValueRef lod = get_src(ctx, instr->src[0].src);
      int tex_idx = get_tex_idx(instr);
      result = fsa_build_txs(&ctx->fsa, lod, tex_idx, info);
      break;
   }
   case nir_texop_txl: {
      fsa_info_t info =
         ctx->stage == MESA_SHADER_VERTEX ? fsa_vs_info : fsa_fs_info;
      LLVMValueRef coord = get_src(ctx, instr->src[0].src);
      LLVMValueRef lod = get_src(ctx, instr->src[1].src);
      int tex_idx = get_tex_idx(instr);
      result = fsa_build_txl(&ctx->fsa, coord, lod, tex_idx, info);
      break;
   }
   default:
      printf("[V_TEX] Type: %d not implemented, op: ", instr->op);
      nir_print_instr(&instr->instr, stdout);
      printf("\n");
      exit(-1);
   }
   if (result) {
      ctx->ssa_defs[instr->def.index] = result;
   }
   return true;
}

static void
visit_load_const(struct fsa_nir_context *ctx, const nir_load_const_instr *instr)
{
   assert(instr->def.num_components == 1);

   ctx->ssa_defs[instr->def.index] = LLVMConstInt(
      LLVMIntTypeInContext(ctx->fsa.context, instr->def.bit_size),
      nir_const_value_as_uint(instr->value[0], instr->def.bit_size), false);
}

static void
visit_phi(struct fsa_nir_context *ctx, nir_phi_instr *instr)
{
   LLVMTypeRef type = get_def_type(ctx, &instr->def);
   LLVMValueRef result = LLVMBuildPhi(ctx->fsa.builder, type, "");

   ctx->ssa_defs[instr->def.index] = result;
   _mesa_hash_table_insert(ctx->phis, instr, result);
}

static LLVMBasicBlockRef
get_block(struct fsa_nir_context *nir, const struct nir_block *b)
{
   struct hash_entry *entry = _mesa_hash_table_search(nir->defs, b);
   return (LLVMBasicBlockRef)entry->data;
}

static void
visit_post_phi(struct fsa_nir_context *ctx, nir_phi_instr *instr,
               LLVMValueRef llvm_phi)
{
   nir_foreach_phi_src (src, instr) {
      LLVMBasicBlockRef block = get_block(ctx, src->pred);
      LLVMValueRef llvm_src = get_src(ctx, src->src);

      LLVMAddIncoming(llvm_phi, &llvm_src, &block, 1);
   }
}

static void
phi_post_pass(struct fsa_nir_context *ctx)
{
   hash_table_foreach (ctx->phis, entry) {
      visit_post_phi(ctx, (nir_phi_instr *)entry->key,
                     (LLVMValueRef)entry->data);
   }
}

static bool
visit_jump(struct fsa_llvm_context *ctx, const nir_jump_instr *instr)
{
   switch (instr->type) {
   case nir_jump_break:
      fsa_build_break(ctx);
      break;
   case nir_jump_continue:
      fsa_build_continue(ctx);
      break;
   default:
      fprintf(stderr, "Unknown NIR jump instr: ");
      nir_print_instr(&instr->instr, stderr);
      fprintf(stderr, "\n");
      return false;
   }
   return true;
}

static void
visit_ssa_undef(struct fsa_nir_context *ctx, const nir_undef_instr *instr)
{
   unsigned num_components = instr->def.num_components;
   LLVMTypeRef type =
      LLVMIntTypeInContext(ctx->fsa.context, instr->def.bit_size);

   LLVMValueRef undef;

   if (num_components == 1)
      undef = LLVMGetUndef(type);
   else {
      undef = LLVMGetUndef(LLVMVectorType(type, num_components));
   }
   ctx->ssa_defs[instr->def.index] = undef;
}

static bool
visit_block(struct fsa_nir_context *ctx, nir_block *block)
{
   LLVMBasicBlockRef blockref = LLVMGetInsertBlock(ctx->fsa.builder);
   LLVMValueRef first = LLVMGetFirstInstruction(blockref);
   if (first) {
      /* LLVM phi nodes must be grouped together and placed at the beginning of
       * each block */
      LLVMPositionBuilderBefore(ctx->fsa.builder,
                                LLVMGetFirstInstruction(blockref));
   }

   nir_foreach_phi (phi, block) {
      visit_phi(ctx, phi);
   }

   nir_foreach_instr (instr, block) {
      switch (instr->type) {
      case nir_instr_type_alu:
         if (!visit_alu(ctx, nir_instr_as_alu(instr)))
            return false;
         break;
      case nir_instr_type_load_const:
         visit_load_const(ctx, nir_instr_as_load_const(instr));
         break;
      case nir_instr_type_intrinsic:
         if (!visit_intrinsic(ctx, nir_instr_as_intrinsic(instr)))
            return false;
         break;
#ifdef FSA_COMPUTE
      case nir_instr_type_deref: {
         nir_deref_instr *deref = nir_instr_as_deref(instr);
         LLVMValueRef value =
            fsa_compute_emit_deref(&ctx->fsa, deref,
                                   deref->deref_type == nir_deref_type_var
                                      ? NULL
                                      : get_src(ctx, deref->parent),
                                   deref->deref_type == nir_deref_type_array
                                      ? get_src(ctx, deref->arr.index)
                                      : NULL);
         if (!value) {
            fprintf(stderr, "Formosa compute: unsupported dereference: ");
            nir_print_instr(instr, stderr);
            fprintf(stderr, "\n");
            return false;
         }
         ctx->ssa_defs[deref->def.index] = value;
         break;
      }
#endif
      case nir_instr_type_tex:
#ifdef FSA_COMPUTE
         if (ctx->stage == MESA_SHADER_COMPUTE) {
            nir_tex_instr *tex = nir_instr_as_tex(instr);
            LLVMValueRef sources[16] = {0}, result = NULL;
            if (tex->num_srcs > ARRAY_SIZE(sources))
               return false;
            for (unsigned i = 0; i < tex->num_srcs; i++)
               sources[i] = get_src(ctx, tex->src[i].src);
            if (!fsa_compute_emit_tex(&ctx->fsa, tex, sources, &result))
               return false;
            ctx->ssa_defs[tex->def.index] = result;
            break;
         }
#endif
         if (!visit_tex(ctx, nir_instr_as_tex(instr)))
            return false;
         break;
      case nir_instr_type_phi:
         break;
      case nir_instr_type_undef:
         visit_ssa_undef(ctx, nir_instr_as_undef(instr));
         break;
      case nir_instr_type_jump:
         if (!visit_jump(&ctx->fsa, nir_instr_as_jump(instr)))
            return false;
         break;
      default:
         printf("[V_BLOCK] Type: %d not implemented,Instr: ", instr->type);
         nir_print_instr(instr, stdout);
         printf("\n");
         if (ctx->stage == MESA_SHADER_COMPUTE)
            return false;
         exit(-1);
      }
   }

   /* Store nir_block to LLVMBlockRef mapping for phi node post pass reference */
   _mesa_hash_table_insert(ctx->defs, block,
                           LLVMGetInsertBlock(ctx->fsa.builder));
   return true;
}

static bool visit_cf_list(struct fsa_nir_context *ctx, struct exec_list *list);
/* nir_if is also a node. recursively visit the then/else statements */
static bool
visit_if(struct fsa_nir_context *ctx, nir_if *if_stmt)
{
   LLVMValueRef value = get_src(ctx, if_stmt->condition);
   nir_block *then_block = (nir_block *)exec_list_get_head(&if_stmt->then_list);
   fsa_build_ifcc(&ctx->fsa, value, then_block->index);

   /* recursively visit */
   if (!visit_cf_list(ctx, &if_stmt->then_list))
      return false;

   if (!exec_list_is_empty(&if_stmt->else_list)) {
      nir_block *else_block =
         (nir_block *)exec_list_get_head(&if_stmt->else_list);

      fsa_build_else(&ctx->fsa, else_block->index);
      if (!visit_cf_list(ctx, &if_stmt->else_list))
         return false;
   }

   fsa_build_endif(&ctx->fsa, then_block->index);
   return true;
}

static bool
visit_loop(struct fsa_nir_context *ctx, nir_loop *loop)
{
   assert(!nir_loop_has_continue_construct(loop));
   nir_block *first_loop_block = (nir_block *)exec_list_get_head(&loop->body);

   fsa_build_bgnloop(&ctx->fsa, first_loop_block->index);

   if (!visit_cf_list(ctx, &loop->body))
      return false;

   fsa_build_endloop(&ctx->fsa, first_loop_block->index);
   return true;
}

static bool
visit_cf_list(struct fsa_nir_context *ctx, struct exec_list *list)
{
   foreach_list_typed (nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_block:
         if (!visit_block(ctx, nir_cf_node_as_block(node))) {
            return false;
         }
         break;
      case nir_cf_node_if:
         if (!visit_if(ctx, nir_cf_node_as_if(node))) {
            return false;
         }
         break;
      case nir_cf_node_loop:
         if (!visit_loop(ctx, nir_cf_node_as_loop(node))) {
            return false;
         }
         break;
      case nir_cf_node_function:
      default:
         printf("[V_CF_LIST] Type %d not implemented\n", node->type);
         if (ctx->stage == MESA_SHADER_COMPUTE)
            return false;
         exit(-1);
         return false;
      }
   }
   return true;
}

/* Do I really need this function? */
static LLVMTypeRef
get_llvm_type_from_glsl_type(struct fsa_llvm_context *ctx,
                             const struct glsl_type *type)
{
   if (glsl_type_is_void(type))
      return ctx->voidt;

   if (glsl_type_is_scalar(type)) {
      if (glsl_type_is_boolean(type))
         return ctx->i1;
      if (glsl_type_is_integer(type))
         return ctx->i32;
      if (glsl_type_is_float(type))
         return ctx->f32;
      if (glsl_type_is_double(type))
         return ctx->f64;
   }

   if (glsl_type_is_vector(type)) {
      const enum glsl_base_type base = glsl_get_base_type(type);
      const int nums = glsl_get_vector_elements(type);
      if (base == GLSL_TYPE_INT) {
         switch (nums) {
         case 2:
            return ctx->v2i32;
         case 3:
            return ctx->v3i32;
         case 4:
            return ctx->v4i32;
         default:
            break;
         }
      } else if (base == GLSL_TYPE_FLOAT) {
         switch (nums) {
         case 2:
            return ctx->v2f32;
         case 3:
            return ctx->v3f32;
         case 4:
            return ctx->v4f32;
         default:
            break;
         }
      }
   }
   fprintf(stdout, "Warning: GLSL type to LLVM type not implemented\n");
   return ctx->voidt;
}

static bool
visit_function(struct fsa_nir_context *ctx, struct nir_function *func)
{
   nir_function_impl *impl = func->impl;
   /* Check is function declaration */
   if (!impl)
      return true;

   LLVMValueRef curr_llvm_func =
      _mesa_hash_table_search(ctx->func_map, func)->data;

   LLVMBasicBlockRef entry_bb =
      LLVMAppendBasicBlockInContext(ctx->fsa.context, curr_llvm_func, "entry");
   LLVMPositionBuilderAtEnd(ctx->fsa.builder, entry_bb);

   if (func->is_entrypoint) {
      /* Perform main function special processing here */
      ctx->main_function = curr_llvm_func;
   }

   if (!visit_cf_list(ctx, &func->impl->body)) {
      return false;
   }

   return true;
}

static void
create_llvm_function(struct fsa_nir_context *ctx, struct nir_function *func)
{
   if (!func->impl)
      return;
   LLVMTypeRef ret_type = ctx->fsa.voidt;
   int num_params = func->num_params;
   LLVMTypeRef *param_types = malloc(sizeof(LLVMTypeRef) * num_params);
   for (int i = 0; i < num_params; i++) {
      nir_parameter *p = &func->params[i];
      LLVMTypeRef t = get_llvm_type_from_glsl_type(&ctx->fsa, p->type);
      param_types[i] = t;
   }
   LLVMValueRef llvm_func =
      LLVMAddFunction(ctx->fsa.module, func->name ? func->name : "unknown",
                      LLVMFunctionType(ret_type, param_types, num_params, 0));
   if (func->is_entrypoint) {
      LLVMSetLinkage(llvm_func, LLVMExternalLinkage);
   } else {
      LLVMSetLinkage(llvm_func, LLVMInternalLinkage);
   }
   LLVMSetFunctionCallConv(llvm_func, LLVMCCallConv);
   _mesa_hash_table_insert(ctx->func_map, func, llvm_func);
   free(param_types);
}

bool
fsa_nir_translate(struct fsa_llvm_context *fsa, struct nir_shader *nir)
{
   struct fsa_nir_context ctx = {0};
   bool ret = true;
   ctx.fsa = *fsa;
   ctx.stage = nir->info.stage;
   ctx.info = &nir->info;
   ctx.func_map = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                          _mesa_key_pointer_equal);

   /* Create LLVM function for each nir function */
   nir_foreach_function (func, nir) {
      if (!ret)
         break;
      create_llvm_function(&ctx, func);
      nir_index_ssa_defs(func->impl);
      ctx.ssa_defs = calloc(func->impl->ssa_alloc, sizeof(LLVMValueRef));

      /* Start lowering function body */
      ctx.defs = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                         _mesa_key_pointer_equal);
      ctx.phis = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                         _mesa_key_pointer_equal);
      if ((ret = visit_function(&ctx, func))) {
         phi_post_pass(&ctx);
      }
      ralloc_free(ctx.defs);
      ralloc_free(ctx.phis);
      free(ctx.ssa_defs);
   }

   _mesa_hash_table_destroy(ctx.func_map, NULL);
   return ret;
}
