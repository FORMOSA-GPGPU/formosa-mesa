#include "fsa_gen_test_nir.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"

#define PREAMBLE(options_ptr)                                                  \
   nir_shader *shader =                                                        \
      nir_shader_create(NULL, MESA_SHADER_VERTEX, options_ptr, NULL);          \
   nir_function *func = nir_function_create(shader, "main");                   \
   func->is_entrypoint = true;                                                 \
   nir_function_impl *impl = nir_function_impl_create(func);                   \
   nir_block *block = nir_start_block(impl);                                   \
   nir_builder b = nir_builder_create(impl);                                   \
   b.cursor = nir_after_block(block);                                          \
   nir_def *const_z = nir_imm_int(&b, 0);

#define POSTAMBLE(result, mask, type_enum)                                     \
   nir_def *offset = nir_imm_int(&b, 0);                                       \
   nir_intrinsic_instr *store = nir_store_output(&b, result, offset);          \
   nir_intrinsic_set_base(store, VARYING_SLOT_VAR0);                           \
   nir_intrinsic_set_write_mask(store, mask);                                  \
   nir_intrinsic_set_component(store, 0);                                      \
   nir_intrinsic_set_src_type(store, type_enum);                               \
                                                                               \
   nir_validate_shader(shader, "manual creation check");                       \
   return shader;

/* * GEN_VARIANT_BINARY: Generates a specific vector version of a binary op
 * ARGS:
 * SHORT:   Short name (e.g., fadd) used for function name and builder call
 * SUFFIX:  scalar, vec2, vec3, vec4
 * COMPS:   Number of components (1, 2, 3, 4)
 * MASK:    Write mask (0x1, 0x3, 0x7, 0xF)
 * TYPE:    Output type (nir_type_float32, etc.)
 */
#define GEN_VARIANT_BINARY(SHORT, SUFFIX, COMPS, MASK, TYPE)                   \
   static nir_shader *gen_##SHORT##_##SUFFIX(                                  \
      const nir_shader_compiler_options *options)                              \
   {                                                                           \
      PREAMBLE(options)                                                        \
      nir_def *src0 = nir_load_input(&b, COMPS, 32, const_z);                  \
      nir_def *src1 = nir_load_input(&b, COMPS, 32, const_z);                  \
      nir_def *res = nir_##SHORT(&b, src0, src1);                              \
      if (res->bit_size == 1) {                                                \
         if (nir_alu_type_get_base_type(TYPE) == nir_type_float) {             \
            res = nir_b2f32(&b, res);                                          \
         } else {                                                              \
            res = nir_b2i32(&b, res);                                          \
         }                                                                     \
      }                                                                        \
      POSTAMBLE(res, MASK, TYPE)                                               \
   }

/* GEN_VARIANT_UNARY: Generates a specific vector version of a unary op */
#define GEN_VARIANT_UNARY(SHORT, SUFFIX, COMPS, MASK, TYPE)                    \
   static nir_shader *gen_##SHORT##_##SUFFIX(                                  \
      const nir_shader_compiler_options *options)                              \
   {                                                                           \
      PREAMBLE(options)                                                        \
      nir_def *src0 = nir_load_input(&b, COMPS, 32, const_z);                  \
      nir_def *res = nir_##SHORT(&b, src0);                                    \
      POSTAMBLE(res, MASK, TYPE)                                               \
   }

/* GEN_VARIANT_TERNARY: Generates a specific vector version of a ternary op */
#define GEN_VARIANT_TERNARY(SHORT, SUFFIX, COMPS, MASK, TYPE)                  \
   static nir_shader *gen_##SHORT##_##SUFFIX(                                  \
      const nir_shader_compiler_options *options)                              \
   {                                                                           \
      PREAMBLE(options)                                                        \
      nir_def *src0 = nir_load_input(&b, COMPS, 32, const_z);                  \
      nir_def *src1 = nir_load_input(&b, COMPS, 32, const_z);                  \
      nir_def *src2 = nir_load_input(&b, COMPS, 32, const_z);                  \
      nir_def *res = nir_##SHORT(&b, src0, src1, src2);                        \
      if (res->bit_size == 1) {                                                \
         if (nir_alu_type_get_base_type(TYPE) == nir_type_float) {             \
            res = nir_b2f32(&b, res);                                          \
         } else {                                                              \
            res = nir_b2i32(&b, res);                                          \
         }                                                                     \
      }                                                                        \
      POSTAMBLE(res, MASK, TYPE)                                               \
   }

#define DEFINE_TERNARY_GROUP(SHORT, TYPE)                                      \
   GEN_VARIANT_TERNARY(SHORT, scalar, 1, 0x1, TYPE)                            \
   GEN_VARIANT_TERNARY(SHORT, vec2, 2, 0x3, TYPE)                              \
   GEN_VARIANT_TERNARY(SHORT, vec3, 3, 0x7, TYPE)                              \
   GEN_VARIANT_TERNARY(SHORT, vec4, 4, 0xF, TYPE)

/* * GROUP MACROS: These call the variant macros 4 times (Scalar -> Vec4)
 */
#define DEFINE_BINARY_GROUP(SHORT, TYPE)                                       \
   GEN_VARIANT_BINARY(SHORT, scalar, 1, 0x1, TYPE)                             \
   GEN_VARIANT_BINARY(SHORT, vec2, 2, 0x3, TYPE)                               \
   GEN_VARIANT_BINARY(SHORT, vec3, 3, 0x7, TYPE)                               \
   GEN_VARIANT_BINARY(SHORT, vec4, 4, 0xF, TYPE)

#define DEFINE_UNARY_GROUP(SHORT, TYPE)                                        \
   GEN_VARIANT_UNARY(SHORT, scalar, 1, 0x1, TYPE)                              \
   GEN_VARIANT_UNARY(SHORT, vec2, 2, 0x3, TYPE)                                \
   GEN_VARIANT_UNARY(SHORT, vec3, 3, 0x7, TYPE)                                \
   GEN_VARIANT_UNARY(SHORT, vec4, 4, 0xF, TYPE)

/* --- Integer/Logic Binary Ops --- */
DEFINE_BINARY_GROUP(iadd, nir_type_int32)
DEFINE_BINARY_GROUP(uadd_sat, nir_type_uint32)
DEFINE_BINARY_GROUP(iadd_sat, nir_type_int32)
DEFINE_BINARY_GROUP(usub_sat, nir_type_uint32)
DEFINE_BINARY_GROUP(isub_sat, nir_type_int32)
DEFINE_BINARY_GROUP(isub, nir_type_int32)
DEFINE_BINARY_GROUP(imul, nir_type_int32)
DEFINE_BINARY_GROUP(imul24_relaxed, nir_type_int32)
DEFINE_BINARY_GROUP(umul24_relaxed, nir_type_uint32)
DEFINE_BINARY_GROUP(iand, nir_type_int32)
DEFINE_BINARY_GROUP(ior, nir_type_int32)
DEFINE_BINARY_GROUP(ixor, nir_type_int32)
DEFINE_BINARY_GROUP(ishl, nir_type_int32)
DEFINE_BINARY_GROUP(ishr, nir_type_int32)
DEFINE_BINARY_GROUP(ushr, nir_type_uint32)
DEFINE_BINARY_GROUP(imax, nir_type_int32)
DEFINE_BINARY_GROUP(imin, nir_type_int32)
DEFINE_BINARY_GROUP(umax, nir_type_uint32)
DEFINE_BINARY_GROUP(umin, nir_type_uint32)

/* --- Float Binary Ops --- */
DEFINE_BINARY_GROUP(fadd, nir_type_float32)
DEFINE_BINARY_GROUP(fsub, nir_type_float32)
DEFINE_BINARY_GROUP(fmul, nir_type_float32)
DEFINE_BINARY_GROUP(fmulz, nir_type_float32)
DEFINE_BINARY_GROUP(fdiv, nir_type_float32)
DEFINE_BINARY_GROUP(fmax, nir_type_float32)
DEFINE_BINARY_GROUP(fmin, nir_type_float32)
DEFINE_BINARY_GROUP(ldexp, nir_type_float32)
DEFINE_BINARY_GROUP(bfm, nir_type_uint32)

/* Ternary Operations */
DEFINE_TERNARY_GROUP(ffma, nir_type_float32)
DEFINE_TERNARY_GROUP(ffmaz, nir_type_float32)
DEFINE_TERNARY_GROUP(bitfield_select, nir_type_uint32)
DEFINE_TERNARY_GROUP(ubfe, nir_type_uint32)
DEFINE_TERNARY_GROUP(ibfe, nir_type_int32)
DEFINE_TERNARY_GROUP(sdot_4x8_iadd, nir_type_int32)
DEFINE_TERNARY_GROUP(sdot_4x8_iadd_sat, nir_type_int32)
DEFINE_TERNARY_GROUP(sudot_4x8_iadd, nir_type_int32)
DEFINE_TERNARY_GROUP(sudot_4x8_iadd_sat, nir_type_int32)
DEFINE_TERNARY_GROUP(udot_4x8_uadd, nir_type_uint32)
DEFINE_TERNARY_GROUP(udot_4x8_uadd_sat, nir_type_uint32)
DEFINE_TERNARY_GROUP(sdot_2x16_iadd, nir_type_int32)
DEFINE_TERNARY_GROUP(udot_2x16_uadd, nir_type_uint32)
DEFINE_TERNARY_GROUP(sdot_2x16_iadd_sat, nir_type_int32)
DEFINE_TERNARY_GROUP(udot_2x16_uadd_sat, nir_type_uint32)
DEFINE_TERNARY_GROUP(shfr, nir_type_int32)
DEFINE_TERNARY_GROUP(msad_4x8, nir_type_uint32)
DEFINE_TERNARY_GROUP(mqsad_4x8, nir_type_uint32)

/* --- Unary Ops (Float & Int) --- */
DEFINE_UNARY_GROUP(fneg, nir_type_float32)
DEFINE_UNARY_GROUP(inot, nir_type_int32)
DEFINE_UNARY_GROUP(frcp, nir_type_float32)
DEFINE_UNARY_GROUP(fabs, nir_type_float32)
DEFINE_UNARY_GROUP(fsat, nir_type_float32)
DEFINE_UNARY_GROUP(iabs, nir_type_int32)
DEFINE_UNARY_GROUP(isign, nir_type_int32)
DEFINE_UNARY_GROUP(fsign, nir_type_float32)
DEFINE_UNARY_GROUP(ffloor, nir_type_float32)
DEFINE_UNARY_GROUP(ftrunc, nir_type_float32)
DEFINE_UNARY_GROUP(fceil, nir_type_float32)
DEFINE_UNARY_GROUP(fround_even, nir_type_float32)
DEFINE_UNARY_GROUP(ffract, nir_type_float32)
DEFINE_UNARY_GROUP(fsqrt, nir_type_float32)
DEFINE_UNARY_GROUP(fexp2, nir_type_float32)
DEFINE_UNARY_GROUP(flog2, nir_type_float32)
DEFINE_UNARY_GROUP(frsq, nir_type_float32)
DEFINE_UNARY_GROUP(frexp_exp, nir_type_int32) /* Returns exponent (int) */
DEFINE_UNARY_GROUP(frexp_sig, nir_type_float32)
DEFINE_UNARY_GROUP(bitfield_reverse, nir_type_uint32)
DEFINE_UNARY_GROUP(bit_count, nir_type_uint32)

/* --- Comparisons (Binary Input -> Bool/Int Output) --- */
DEFINE_BINARY_GROUP(ilt, nir_type_int32)
DEFINE_BINARY_GROUP(ine, nir_type_int32)
DEFINE_BINARY_GROUP(ieq, nir_type_int32)
DEFINE_BINARY_GROUP(ige, nir_type_int32)
DEFINE_BINARY_GROUP(ult, nir_type_uint32)
DEFINE_BINARY_GROUP(uge, nir_type_uint32)
DEFINE_BINARY_GROUP(feq, nir_type_int32)
DEFINE_BINARY_GROUP(fneu, nir_type_int32)
DEFINE_BINARY_GROUP(fequ, nir_type_int32)
DEFINE_BINARY_GROUP(fneo, nir_type_int32)
DEFINE_BINARY_GROUP(flt, nir_type_int32)
DEFINE_BINARY_GROUP(fge, nir_type_int32)
DEFINE_BINARY_GROUP(fltu, nir_type_int32)
DEFINE_BINARY_GROUP(fgeu, nir_type_int32)
DEFINE_BINARY_GROUP(funord, nir_type_int32)
DEFINE_BINARY_GROUP(ford, nir_type_int32)

#define ENTRY(SHORT, SUFFIX, DESC_PREFIX)                                      \
   {                                                                           \
      #SHORT "_" #SUFFIX, DESC_PREFIX "-load," #SHORT ",store",                \
         gen_##SHORT##_##SUFFIX                                                \
   }

#define ENTRY_GROUP(SHORT)                                                     \
   ENTRY(SHORT, scalar, "Scalar"), ENTRY(SHORT, vec2, "Vec2"),                 \
      ENTRY(SHORT, vec3, "Vec3"), ENTRY(SHORT, vec4, "Vec4")

struct fsa_nir_microbench_struct fsa_nir_benchlist[] = {
   ENTRY_GROUP(fneg),
   ENTRY_GROUP(inot),
   ENTRY_GROUP(iadd),
   ENTRY_GROUP(uadd_sat),
   ENTRY_GROUP(iadd_sat),
   ENTRY_GROUP(usub_sat),
   ENTRY_GROUP(isub_sat),
   ENTRY_GROUP(fsub),
   ENTRY_GROUP(isub),
   ENTRY_GROUP(imul),
   ENTRY_GROUP(imul24_relaxed),
   ENTRY_GROUP(umul24_relaxed),
   ENTRY_GROUP(fmul),
   ENTRY_GROUP(fmulz),
   ENTRY_GROUP(frcp),
   ENTRY_GROUP(iand),
   ENTRY_GROUP(ior),
   ENTRY_GROUP(ixor),
   ENTRY_GROUP(ishl),
   ENTRY_GROUP(ishr),
   ENTRY_GROUP(ushr),
   ENTRY_GROUP(ilt),
   ENTRY_GROUP(ine),
   ENTRY_GROUP(ieq),
   ENTRY_GROUP(ige),
   ENTRY_GROUP(ult),
   ENTRY_GROUP(uge),
   ENTRY_GROUP(fdiv),
   ENTRY_GROUP(feq),
   ENTRY_GROUP(fneu),
   ENTRY_GROUP(fequ),
   ENTRY_GROUP(fneo),
   ENTRY_GROUP(flt),
   ENTRY_GROUP(fge),
   ENTRY_GROUP(fltu),
   ENTRY_GROUP(fgeu),
   ENTRY_GROUP(funord),
   ENTRY_GROUP(ford),
   ENTRY_GROUP(fabs),
   ENTRY_GROUP(fsat),
   ENTRY_GROUP(iabs),
   ENTRY_GROUP(imax),
   ENTRY_GROUP(imin),
   ENTRY_GROUP(umax),
   ENTRY_GROUP(umin),
   ENTRY_GROUP(isign),
   ENTRY_GROUP(fsign),
   ENTRY_GROUP(ffloor),
   ENTRY_GROUP(ftrunc),
   ENTRY_GROUP(fceil),
   ENTRY_GROUP(fround_even),
   ENTRY_GROUP(ffract),
   ENTRY_GROUP(fsqrt),
   ENTRY_GROUP(fexp2),
   ENTRY_GROUP(flog2),
   ENTRY_GROUP(frsq),
   ENTRY_GROUP(frexp_exp),
   ENTRY_GROUP(frexp_sig),
   ENTRY_GROUP(fmax),
   ENTRY_GROUP(fmin),
   ENTRY_GROUP(ldexp),
   ENTRY_GROUP(ffma),
   ENTRY_GROUP(ffmaz),
   ENTRY_GROUP(bitfield_reverse),
   ENTRY_GROUP(bit_count),
   ENTRY_GROUP(bfm),
   ENTRY_GROUP(bitfield_select),
   ENTRY_GROUP(ubfe),
   ENTRY_GROUP(ibfe),
   ENTRY_GROUP(sdot_4x8_iadd),
   ENTRY_GROUP(sdot_4x8_iadd_sat),
   ENTRY_GROUP(sudot_4x8_iadd),
   ENTRY_GROUP(sudot_4x8_iadd_sat),
   ENTRY_GROUP(udot_4x8_uadd),
   ENTRY_GROUP(udot_4x8_uadd_sat),
   ENTRY_GROUP(sdot_2x16_iadd),
   ENTRY_GROUP(udot_2x16_uadd),
   ENTRY_GROUP(sdot_2x16_iadd_sat),
   ENTRY_GROUP(udot_2x16_uadd_sat),
   ENTRY_GROUP(shfr),
   ENTRY_GROUP(msad_4x8),
   ENTRY_GROUP(mqsad_4x8),
   {NULL, NULL, NULL},
};
