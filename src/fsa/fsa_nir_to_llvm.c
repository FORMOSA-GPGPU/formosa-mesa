#include "fsa_nir_to_llvm.h"
#include "fsa_llvm_build.h"

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
   case nir_op_imul24_relaxed:
   case nir_op_umul24_relaxed:
      result = LLVMBuildMul(ctx->fsa.builder, src[0], src[1], "");
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
         emit_intrin_1f_param(&ctx->fsa, "llvm.rint",
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

static LLVMValueRef
visit_load(struct fsa_nir_context *ctx, nir_intrinsic_instr *instr)
{
   LLVMValueRef values[8];
   LLVMTypeRef dest_type = get_def_type(ctx, &instr->def);
   /*
      base = which slot , e.g. layout(location = 5) -> base = 5
      component = which component to start, .y -> component = 1
      count = how many comps to read, .yz -> count = 2
      offset = array index, there is type like in vec3[4] aPos
   */
   unsigned base = nir_intrinsic_base(instr);
   unsigned component = nir_intrinsic_component(instr);
   unsigned count = instr->def.num_components;
   nir_src offset = *nir_get_io_offset_src(instr);

   LLVMValueRef result = LLVMGetUndef(dest_type);
   /* Scalar */
   if (LLVMGetTypeKind(dest_type) != LLVMVectorTypeKind) {
      LLVMValueRef input_addr = LLVMConstInt(ctx->fsa.i64, 87654321, 0);
      return fsa_build_load_input_to_scalar(&ctx->fsa, input_addr, result);
   }

   /* Cector */
   if (ctx->stage == MESA_SHADER_VERTEX) {
      for (unsigned chan = 0; chan < count; chan++) {
         /* Fake input address, TODO */
         LLVMValueRef input_addr =
            LLVMConstInt(ctx->fsa.i64, 0x87654300 + chan * 8, 0);
         LLVMValueRef idx = LLVMConstInt(ctx->fsa.i32, chan, 0);
         result =
            fsa_build_load_input_to_vector(&ctx->fsa, input_addr, result, idx);
      }
   } else if (ctx->stage == MESA_SHADER_FRAGMENT) {
      for (unsigned chan = 0; chan < count; chan++) {
         /* Fake input address, TODO */
         LLVMValueRef input_addr =
            LLVMConstInt(ctx->fsa.i64, 0x87654300 + chan * 8, 0);
         LLVMValueRef idx = LLVMConstInt(ctx->fsa.i32, chan, 0);
         result =
            fsa_build_load_input_to_vector(&ctx->fsa, input_addr, result, idx);
      }
   }

   return result;
}

static void
visit_store_output(struct fsa_nir_context *ctx, nir_intrinsic_instr *instr)
{
   unsigned base = nir_intrinsic_base(instr);
   unsigned writemask = nir_intrinsic_write_mask(instr);
   unsigned component = nir_intrinsic_component(instr);
   LLVMValueRef src = fsa_to_float(&ctx->fsa, get_src(ctx, instr->src[0]));
   ASSERTED unsigned bit_size = fsa_get_elem_bits(&ctx->fsa, LLVMTypeOf(src));
   ASSERTED nir_src offset = *nir_get_io_offset_src(instr);

   LLVMTypeRef elem_type = LLVMTypeOf(src);
   if (LLVMGetTypeKind(elem_type) == LLVMVectorTypeKind) {
      elem_type = LLVMGetElementType(elem_type);
   }

   /*
      base = which slot
      writemask start at bit 0
      component = which component to start
      e.g. out_var.yz = vec2(val1, val2)
      writemask = 0011, component = 1
      writemask <<= 1 -> 0110
   */

   writemask <<= component;

   for (unsigned chan = 0; chan < 8; chan++) {
      if (!(writemask & (1 << chan)))
         continue;
      LLVMValueRef value = fsa_llvm_extract_elem(&ctx->fsa, src, chan);
      /* Fake output address, TODO */
      LLVMValueRef output_addr =
         LLVMConstInt(ctx->fsa.i64, 12345600 + chan * 8, 0);
      LLVMValueRef output_ptr =
         fsa_get_pointer_val(&ctx->fsa, elem_type, output_addr);
      LLVMBuildStore(ctx->fsa.builder, value, output_ptr);
   }
}

static bool
visit_intrinsic(struct fsa_nir_context *ctx, nir_intrinsic_instr *instr)
{
   LLVMValueRef result = NULL;
   switch (instr->intrinsic) {
   case nir_intrinsic_load_input:
      result = visit_load(ctx, instr);
      break;
   case nir_intrinsic_store_output:
      visit_store_output(ctx, instr);
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

static void
visit_load_const(struct fsa_nir_context *ctx, const nir_load_const_instr *instr)
{
   assert(instr->def.num_components == 1);

   ctx->ssa_defs[instr->def.index] = LLVMConstInt(
      LLVMIntTypeInContext(ctx->fsa.context, instr->def.bit_size),
      nir_const_value_as_uint(instr->value[0], instr->def.bit_size), false);
}

static bool
visit_block(struct fsa_nir_context *ctx, nir_block *block)
{
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
         visit_intrinsic(ctx, nir_instr_as_intrinsic(instr));
         break;
      default:
         printf("[V_BLOCK] Type: %d not implemented,Instr: ", instr->type);
         nir_print_instr(instr, stdout);
         printf("\n");
         exit(-1);
      }
   }
   LLVMGetInsertBlock(ctx->fsa.builder);
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
      case nir_cf_node_loop:
      case nir_cf_node_function:
      default:
         printf("[V_CF_LIST] Type %d not implemented\n", node->type);
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
   ctx.fsa = *fsa;
   ctx.stage = nir->info.stage;
   ctx.info = &nir->info;
   ctx.func_map = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                          _mesa_key_pointer_equal);

   /* Create LLVM function for each nir function */
   nir_foreach_function (func, nir) {
      create_llvm_function(&ctx, func);
      ctx.ssa_defs =
         realloc(ctx.ssa_defs, sizeof(LLVMValueRef) * func->impl->ssa_alloc);
   }

   /* Start lowering function body */
   nir_foreach_function (func, nir) {
      ctx.defs = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                         _mesa_key_pointer_equal);
      if (!visit_function(&ctx, func)) {
         return false;
      }
      _mesa_hash_table_destroy(ctx.defs, NULL);
   }

   _mesa_hash_table_destroy(ctx.func_map, NULL);
   return true;
}