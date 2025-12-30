#include "fsa_gen_test_nir.h"
#include <math.h>
#include <stdlib.h>
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "softfloat.h"

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

#define ENTRY(SHORT, SUFFIX, DESC_PREFIX, TYPE, NUM_ELE, GOLDEN)               \
   {                                                                           \
      #SHORT "_" #SUFFIX, DESC_PREFIX "-load," #SHORT ",store",                \
         gen_##SHORT##_##SUFFIX, TYPE, NUM_ELE, GOLDEN,                        \
   }

#define ENTRY_GROUP(SHORT, TYPE, GOLDEN)                                       \
   ENTRY(SHORT, scalar, "Scalar", TYPE, 1, GOLDEN),                            \
      ENTRY(SHORT, vec2, "Vec2", TYPE, 2, GOLDEN),                             \
      ENTRY(SHORT, vec3, "Vec3", TYPE, 3, GOLDEN),                             \
      ENTRY(SHORT, vec4, "Vec4", TYPE, 4, GOLDEN)

static void
gen_golden_if(void *dest, void *a, void *b, void *c)
{
   int32_t *ia = (int32_t *)a;
   int32_t *ib = (int32_t *)b;
   *(int32_t *)dest = *ia < *ib ? *ia : *ib;
}

static nir_shader *
gen_if_scalar(const nir_shader_compiler_options *option)
{
   PREAMBLE(option);
   nir_def *src0 = nir_load_input(&b, 1, 32, const_z);
   nir_def *src1 = nir_load_input(&b, 1, 32, const_z);
   nir_def *cond = nir_ilt(&b, src0, src1);
   nir_if *nif = nir_push_if(&b, cond);
   nir_def *res = NULL;
   {
      res = src0;
      nir_def *offset = nir_imm_int(&b, 0);
      nir_intrinsic_instr *store = nir_store_output(&b, res, offset);
      nir_intrinsic_set_base(store, VARYING_SLOT_VAR0);
      nir_intrinsic_set_write_mask(store, 0x1);
      nir_intrinsic_set_component(store, 0);
      nir_intrinsic_set_src_type(store, nir_type_int32);
   }
   nir_push_else(&b, nif);
   {
      res = src1;
      nir_def *offset = nir_imm_int(&b, 0);
      nir_intrinsic_instr *store = nir_store_output(&b, res, offset);
      nir_intrinsic_set_base(store, VARYING_SLOT_VAR0);
      nir_intrinsic_set_write_mask(store, 0x1);
      nir_intrinsic_set_component(store, 0);
      nir_intrinsic_set_src_type(store, nir_type_int32);
   }
   nir_pop_if(&b, nif);
   nir_validate_shader(shader, "manual creation check");
   return shader;
}

static void
gen_golden_if_phi(void *dest, void *a, void *b, void *c)
{
   int32_t *ia = (int32_t *)a;
   int32_t *ib = (int32_t *)b;
   int32_t *ic = (int32_t *)c;
   if (*ia < *ib) {
      *(int32_t*)dest = *ib + *ic;
   } else {
      *(int32_t*)dest = *ia + *ib;
   }
}

static nir_shader *
gen_if_phi_scalar(const nir_shader_compiler_options *option)
{
   PREAMBLE(option);
   nir_def *src0 = nir_load_input(&b, 1, 32, const_z);
   nir_def *src1 = nir_load_input(&b, 1, 32, const_z);
   nir_def *src2 = nir_load_input(&b, 1, 32, const_z);
   nir_def *res0 = NULL, *res1 = NULL;
   nir_block *then_block = NULL, *else_block = NULL;
   nir_if *nif = nir_push_if(&b, nir_ilt(&b, src0, src1));
   {
      then_block = nir_cursor_current_block(b.cursor);
      nir_def *tmp = nir_iadd(&b, src1, src2);
      res0 = tmp;
   }
   nir_push_else(&b, nif);
   {
      else_block = nir_cursor_current_block(b.cursor);
      nir_def *tmp = nir_iadd(&b, src0, src1);
      res1 = tmp;
   }
   nir_pop_if(&b, nif);

   /* Construct phi node */
   nir_phi_instr *store_val_phi = nir_phi_instr_create(b.shader);
   nir_def_init(&store_val_phi->instr, &store_val_phi->def, 1, 32);
   nir_phi_instr_add_src(store_val_phi, then_block, res0);
   nir_phi_instr_add_src(store_val_phi, else_block, res1);
   nir_builder_instr_insert(&b, &store_val_phi->instr);

   POSTAMBLE(&store_val_phi->def, 0x1, nir_type_int32);
}

static nir_shader *
gen_if_var_scalar(const nir_shader_compiler_options *option)
{
   PREAMBLE(option);

   nir_variable *res_var =
      nir_local_variable_create(b.impl, glsl_int_type(), "res_var");

   nir_def *src0 = nir_load_input(&b, 1, 32, const_z);
   nir_def *src1 = nir_load_input(&b, 1, 32, const_z);
   nir_def *src2 = nir_load_input(&b, 1, 32, const_z);

   nir_if *nif = nir_push_if(&b, nir_ilt(&b, src0, src1));
   {
      nir_def *tmp = nir_iadd(&b, src1, src2);
      nir_store_var(&b, res_var, tmp, 0x1);
   }
   nir_push_else(&b, nif);
   {
      nir_def *tmp = nir_iadd(&b, src0, src1);
      nir_store_var(&b, res_var, tmp, 0x1);
   }
   nir_pop_if(&b, nif);

   nir_def *final_val = nir_load_var(&b, res_var);
   POSTAMBLE(final_val, 0x1, nir_type_int32);
}

static void
gen_golden_loop(void *dest, void *a, void *b, void *c)
{
   int32_t *ia = (int32_t *)a;
   uint32_t sum = 0;
   for (int i = 0; i < 10; i++) {
      sum += *ia;
   }
   *(int32_t *)dest = sum;
}

/* loop accum using local var */
static nir_shader *
gen_loop(const nir_shader_compiler_options *option)
{
   PREAMBLE(option);
   nir_def *src0 = nir_load_input(&b, 1, 32, const_z);
   nir_def *init_counter = nir_imm_int(&b, 0);
   nir_def *init_sum = nir_imm_int(&b, 0);
   nir_def *limit = nir_imm_int(&b, 10);

   nir_block *pred_block = nir_cursor_current_block(b.cursor);

   nir_loop *loop = nir_push_loop(&b);
   nir_block *header_block = nir_cursor_current_block(b.cursor);
   /* Counter Phi */
   nir_phi_instr *counter_phi = nir_phi_instr_create(b.shader);
   nir_def_init(&counter_phi->instr, &counter_phi->def, 1, 32);

   /* Accum Phi*/
   nir_phi_instr *sum_phi = nir_phi_instr_create(b.shader);
   nir_def_init(&sum_phi->instr, &sum_phi->def, 1, 32);

   /* Phi choose value from outside or in the loop */
   nir_phi_instr_add_src(counter_phi, pred_block, init_counter);
   nir_phi_instr_add_src(sum_phi, pred_block, init_sum);

   nir_def *current_counter = &counter_phi->def;
   nir_def *current_sum = &sum_phi->def;

   /* Loop start */
   nir_def *cmp = nir_ige(&b, current_counter, limit); // i >= 10
   nir_if *nif = nir_push_if(&b, cmp);
   {
      nir_jump(&b, nir_jump_break);
   }
   nir_pop_if(&b, nif);
   /* Loop body */
   nir_def *next_sum = nir_iadd(&b, current_sum, src0);
   nir_def *next_counter = nir_iadd_imm(&b, current_counter, 1);

   nir_block *back_edge_block = nir_cursor_current_block(b.cursor);
   nir_phi_instr_add_src(counter_phi, back_edge_block, next_counter);
   nir_phi_instr_add_src(sum_phi, back_edge_block, next_sum);

   /* Insert phi node last */
   nir_cursor end_of_loop_cursor = b.cursor;
   b.cursor = nir_before_block(header_block);
   nir_builder_instr_insert(&b, &counter_phi->instr);
   nir_builder_instr_insert(&b, &sum_phi->instr);
   b.cursor = end_of_loop_cursor;

   nir_pop_loop(&b, loop);

   current_sum = &sum_phi->def;
   nir_def *res = current_sum;
   POSTAMBLE(res, 0x1, nir_type_uint32);
}

static const float32_t f32_zero = {0x00000000};
static const float32_t f32_one = {0x3f800000};
static const float32_t f32_neg_one = {0xbf800000};

static inline bool
f32_isnan(float32_t a)
{
   return !f32_eq(a, a);
}

/* rand() only generates 16 bit values */
static uint32_t
rand_uint32()
{
   uint32_t r1 = (uint32_t)rand() & 0xFFFF;
   uint32_t r2 = (uint32_t)rand() & 0xFFFF;
   return r1 << 16 | r2;
}

uint32_t
gen_valid_float()
{
   uint32_t u;
   const uint32_t EXPONENT_MASK = 0x7F800000;
   while (1) {
      u = rand_uint32();
      /* Check for NaN and Infs */
      if ((u & EXPONENT_MASK) == EXPONENT_MASK) {
         continue;
      }

      /* u is valid float here */
      break;
   }
   return u;
}

/* Helper for saturating signed addition */
static inline int32_t
add_sat_s32(int32_t a, int32_t b)
{
   int64_t res = (int64_t)a + b;
   if (res > INT32_MAX)
      return INT32_MAX;
   if (res < INT32_MIN)
      return INT32_MIN;
   return (int32_t)res;
}

/* Helper for saturating unsigned addition */
static inline uint32_t
add_sat_u32(uint32_t a, uint32_t b)
{
   uint64_t res = (uint64_t)a + b;
   if (res > UINT32_MAX)
      return UINT32_MAX;
   return (uint32_t)res;
}

/* Helper to extract signed 8-bit lane */
static inline int8_t
extract_i8(uint32_t v, int lane)
{
   return (int8_t)((v >> (lane * 8)) & 0xFF);
}

/* Helper to extract unsigned 8-bit lane */
static inline uint8_t
extract_u8(uint32_t v, int lane)
{
   return (uint8_t)((v >> (lane * 8)) & 0xFF);
}

/* Helper to extract signed 16-bit lane */
static inline int16_t
extract_i16(uint32_t v, int lane)
{
   return (int16_t)((v >> (lane * 16)) & 0xFFFF);
}

/* Helper to extract unsigned 16-bit lane */
static inline uint16_t
extract_u16(uint32_t v, int lane)
{
   return (uint16_t)((v >> (lane * 16)) & 0xFFFF);
}

static void
gen_golden_iadd(void *dest, void *a, void *b, void *c)
{
   int32_t *ia = (int32_t *)a;
   int32_t *ib = (int32_t *)b;
   *(int32_t *)dest = *ia + *ib;
}

/* If result is smaller than operand (wraps around), overflow occurred */
static void
gen_golden_uadd_sat(void *dest, void *a, void *b, void *c)
{
   uint32_t ua = *(uint32_t *)a;
   uint32_t ub = *(uint32_t *)b;
   uint32_t res = add_sat_u32(ua, ub);
   *(uint32_t *)dest = res;
}

static void
gen_golden_iadd_sat(void *dest, void *a, void *b, void *c)
{
   int64_t ia = *(int32_t *)a;
   int64_t ib = *(int32_t *)b;
   int64_t res = add_sat_s32(ia, ib);
   *(int32_t *)dest = (int32_t)res;
}

static void
gen_golden_usub_sat(void *dest, void *a, void *b, void *c)
{
   uint32_t ua = *(uint32_t *)a;
   uint32_t ub = *(uint32_t *)b;

   if (ua < ub) {
      *(uint32_t *)dest = 0;
   } else {
      *(uint32_t *)dest = ua - ub;
   }
}

static void
gen_golden_isub_sat(void *dest, void *a, void *b, void *c)
{
   int64_t ia = *(int32_t *)a;
   int64_t ib = *(int32_t *)b;
   int64_t res = ia - ib;

   if (res > INT32_MAX) {
      *(int32_t *)dest = INT32_MAX;
   } else if (res < INT32_MIN) {
      *(int32_t *)dest = INT32_MIN;
   } else {
      *(int32_t *)dest = (int32_t)res;
   }
}

static void
gen_golden_isub(void *dest, void *a, void *b, void *c)
{
   *(int32_t *)dest = *(int32_t *)a - *(int32_t *)b;
}

static void
gen_golden_imul(void *dest, void *a, void *b, void *c)
{
   *(int32_t *)dest = *(int32_t *)a * *(int32_t *)b;
}

/* * imul24 typically treats inputs as 24-bit signed integers.
 * We simulate this by sign-extending from the 24th bit.
 */
static void
gen_golden_imul24_relaxed(void *dest, void *a, void *b, void *c)
{
   int32_t ia = (*(int32_t *)a << 8) >> 8;
   int32_t ib = (*(int32_t *)b << 8) >> 8;
   *(int32_t *)dest = ia * ib;
}

static void
gen_golden_umul24_relaxed(void *dest, void *a, void *b, void *c)
{
   /* Mask inputs to 24 bits */
   uint32_t ua = *(uint32_t *)a & 0xFFFFFF;
   uint32_t ub = *(uint32_t *)b & 0xFFFFFF;
   *(uint32_t *)dest = ua * ub;
}

static void
gen_golden_iand(void *dest, void *a, void *b, void *c)
{
   *(uint32_t *)dest = *(uint32_t *)a & *(uint32_t *)b;
}

static void
gen_golden_ior(void *dest, void *a, void *b, void *c)
{
   *(uint32_t *)dest = *(uint32_t *)a | *(uint32_t *)b;
}

static void
gen_golden_ixor(void *dest, void *a, void *b, void *c)
{
   *(uint32_t *)dest = *(uint32_t *)a ^ *(uint32_t *)b;
}

static void
gen_golden_ishl(void *dest, void *a, void *b, void *c)
{
   /* Mask shift amount by 31 to match standard GPU behavior */
   uint32_t shift = *(uint32_t *)b & 0x1f;
   *(uint32_t *)dest = *(uint32_t *)a << shift;
}

static void
gen_golden_ishr(void *dest, void *a, void *b, void *c)
{
   uint32_t shift = *(uint32_t *)b & 0x1f;
   *(int32_t *)dest = *(int32_t *)a >> shift;
}

static void
gen_golden_ushr(void *dest, void *a, void *b, void *c)
{
   uint32_t shift = *(uint32_t *)b & 0x1f;
   *(uint32_t *)dest = *(uint32_t *)a >> shift;
}

static void
gen_golden_imax(void *dest, void *a, void *b, void *c)
{
   int32_t ia = *(int32_t *)a;
   int32_t ib = *(int32_t *)b;
   *(int32_t *)dest = ia > ib ? ia : ib;
}

static void
gen_golden_imin(void *dest, void *a, void *b, void *c)
{
   int32_t ia = *(int32_t *)a;
   int32_t ib = *(int32_t *)b;
   *(int32_t *)dest = ia < ib ? ia : ib;
}

static void
gen_golden_umax(void *dest, void *a, void *b, void *c)
{
   uint32_t ua = *(uint32_t *)a;
   uint32_t ub = *(uint32_t *)b;
   *(uint32_t *)dest = ua > ub ? ua : ub;
}

static void
gen_golden_umin(void *dest, void *a, void *b, void *c)
{
   uint32_t ua = *(uint32_t *)a;
   uint32_t ub = *(uint32_t *)b;
   *(uint32_t *)dest = ua < ub ? ua : ub;
}

static void
gen_golden_fadd(void *dest, void *a, void *b, void *c)
{
   float32_t fa = {*(uint32_t *)a};
   float32_t fb = {*(uint32_t *)b};
   float32_t res = f32_add(fa, fb);
   *(uint32_t *)dest = res.v;
}

static void
gen_golden_fsub(void *dest, void *a, void *b, void *c)
{
   float32_t fa = {*(uint32_t *)a};
   float32_t fb = {*(uint32_t *)b};
   float32_t res = f32_sub(fa, fb);
   *(uint32_t *)dest = res.v;
}

static void
gen_golden_fmul(void *dest, void *a, void *b, void *c)
{
   float32_t fa = {*(uint32_t *)a};
   float32_t fb = {*(uint32_t *)b};
   float32_t res = f32_mul(fa, fb);
   *(uint32_t *)dest = res.v;
}

/* * Note: fmulz typically implies "multiply with zero wins" (0 * Inf = 0)
 * or flush-to-zero semantics depending on the GPU.
 * Defaulting to standard f32_mul here. If strict "zero wins"
 * is required, manual checks for zero operands are needed.
 */
static void
gen_golden_fmulz(void *dest, void *a, void *b, void *c)
{
   float32_t fa = {*(uint32_t *)a};
   float32_t fb = {*(uint32_t *)b};
   float32_t res = f32_mul(fa, fb);
   *(uint32_t *)dest = res.v;
}

static void
gen_golden_fdiv(void *dest, void *a, void *b, void *c)
{
   float32_t fa = {*(uint32_t *)a};
   float32_t fb = {*(uint32_t *)b};
   float32_t res = f32_div(fa, fb);
   *(uint32_t *)dest = res.v;
}

static void
gen_golden_fmax(void *dest, void *a, void *b, void *c)
{
   float32_t fa = {*(uint32_t *)a};
   float32_t fb = {*(uint32_t *)b};
   float32_t res = f32_lt(fa, fb) ? fb : fa;
   *(uint32_t *)dest = res.v;
}

static void
gen_golden_fmin(void *dest, void *a, void *b, void *c)
{
   float32_t fa = {*(uint32_t *)a};
   float32_t fb = {*(uint32_t *)b};
   float32_t res = f32_lt(fa, fb) ? fa : fb;
   *(uint32_t *)dest = res.v;
}

static void
gen_golden_ldexp(void *dest, void *a, void *b, void *c)
{
   /* NIR ldexp: src0 = float, src1 = int (exponent) */
   float fa = *(float *)a;
   int32_t exp = *(int32_t *)b;
   float res = ldexpf(fa, exp);
   *(float *)dest = res;
}

/* bit field move */
static void
gen_golden_bfm(void *dest, void *a, void *b, void *c)
{
   /* NIR bfm: src0 = bits (width), src1 = offset */
   uint32_t bits = *(uint32_t *)a & 0x1F;
   uint32_t offset = *(uint32_t *)b & 0x1F;

   /* Mask generation: ((1 << bits) - 1) << offset */
   /* Handle the edge case where bits == 32 to avoid shift overflow */
   uint32_t mask = (1u << bits) - 1u;
   *(uint32_t *)dest = mask << offset;
}

static void
gen_golden_fneg(void *dest, void *a, void *b, void *c)
{
   uint32_t ua = *(uint32_t *)a;
   *(uint32_t *)dest = ua ^ 0x80000000;
}

static void
gen_golden_inot(void *dest, void *a, void *b, void *c)
{
   *(uint32_t *)dest = ~(*(uint32_t *)a);
}

static void
gen_golden_frcp(void *dest, void *a, void *b, void *c)
{
   float32_t fa = {*(uint32_t *)a};
   float32_t res = f32_div(f32_one, fa);
   *(uint32_t *)dest = res.v;
}

static void
gen_golden_fabs(void *dest, void *a, void *b, void *c)
{
   uint32_t ua = *(uint32_t *)a;
   *(uint32_t *)dest = ua & 0x7FFFFFFF;
}

static void
gen_golden_fsat(void *dest, void *a, void *b, void *c)
{
   float32_t fa = {*(uint32_t *)a};
   // NIR fsat: clamp(x, 0.0, 1.0). NaN usually becomes 0.0.
   // Using softfloat comparisons.
   if (f32_lt(fa, f32_zero)) {
      *(uint32_t *)dest = f32_zero.v;
   } else if (f32_lt(f32_one, fa)) {
      *(uint32_t *)dest = f32_one.v;
   } else {
      // Handle NaN: if it's NaN, f32_lt returns false for both.
      // We need to verify if fa is NaN.
      // For simplicity here, if not < 0 and not > 1, we pass through.
      // However, correct fsat(NaN) is 0.0.
      if ((fa.v & 0x7FFFFFFF) > 0x7F800000) {
         *(uint32_t *)dest = f32_zero.v;
      } else {
         *(uint32_t *)dest = fa.v;
      }
   }
}

static void
gen_golden_iabs(void *dest, void *a, void *b, void *c)
{
   int32_t ia = *(int32_t *)a;
   *(int32_t *)dest = (ia < 0) ? -ia : ia;
}

static void
gen_golden_isign(void *dest, void *a, void *b, void *c)
{
   int32_t ia = *(int32_t *)a;
   *(int32_t *)dest = (ia == 0) ? 0 : ((ia > 0) ? 1 : -1);
}

static void
gen_golden_fsign(void *dest, void *a, void *b, void *c)
{
   float32_t fa = {*(uint32_t *)a};
   // Check for Zero (ignoring sign bit for zero check)
   if ((fa.v & 0x7FFFFFFF) == 0) {
      *(uint32_t *)dest = 0; // Returns 0.0
   } else {
      // If MSB set (negative) return -1.0, else 1.0
      *(uint32_t *)dest = (fa.v & 0x80000000) ? f32_neg_one.v : f32_one.v;
   }
}

static void
gen_golden_ffloor(void *dest, void *a, void *b, void *c)
{
   float32_t fa = {*(uint32_t *)a};
   // softfloat_round_min = Round toward -infinity
   float32_t res = f32_roundToInt(fa, softfloat_round_min, false);
   *(uint32_t *)dest = res.v;
}

static void
gen_golden_ftrunc(void *dest, void *a, void *b, void *c)
{
   float32_t fa = {*(uint32_t *)a};
   // softfloat_round_minMag = Round toward zero
   float32_t res = f32_roundToInt(fa, softfloat_round_minMag, false);
   *(uint32_t *)dest = res.v;
}

static void
gen_golden_fceil(void *dest, void *a, void *b, void *c)
{
   float32_t fa = {*(uint32_t *)a};
   // softfloat_round_max = Round toward +infinity
   float32_t res = f32_roundToInt(fa, softfloat_round_max, false);
   *(uint32_t *)dest = res.v;
}

static void
gen_golden_fround_even(void *dest, void *a, void *b, void *c)
{
   float32_t fa = {*(uint32_t *)a};
   // softfloat_round_near_even
   float32_t res = f32_roundToInt(fa, softfloat_round_near_even, false);
   *(uint32_t *)dest = res.v;
}

static void
gen_golden_ffract(void *dest, void *a, void *b, void *c)
{
   // fract(x) = x - floor(x)
   float32_t fa = {*(uint32_t *)a};
   float32_t flr = f32_roundToInt(fa, softfloat_round_min, false);
   float32_t res = f32_sub(fa, flr);
   *(uint32_t *)dest = res.v;
}

static void
gen_golden_fsqrt(void *dest, void *a, void *b, void *c)
{
   float32_t fa = {*(uint32_t *)a};
   float32_t res = f32_sqrt(fa);
   *(uint32_t *)dest = res.v;
}

static void
gen_golden_fexp2(void *dest, void *a, void *b, void *c)
{
   // Softfloat usually doesn't include exp2, using standard math library
   float fa = *(float *)a;
   float res = exp2f(fa);
   *(float *)dest = res;
}

static void
gen_golden_flog2(void *dest, void *a, void *b, void *c)
{
   float fa = *(float *)a;
   float res = log2f(fa);
   *(float *)dest = res;
}

static void
gen_golden_frsq(void *dest, void *a, void *b, void *c)
{
   float32_t fa = {*(uint32_t *)a};
   // 1.0 / sqrt(x)
   float32_t res = f32_div(f32_one, f32_sqrt(fa));
   *(uint32_t *)dest = res.v;
}

static void
gen_golden_frexp_exp(void *dest, void *a, void *b, void *c)
{
   float fa = *(float *)a;
   int exp;
   frexpf(fa, &exp);
   // NIR frexp_exp returns the exponent as an integer (int32)
   *(int32_t *)dest = (int32_t)exp;
}

static void
gen_golden_frexp_sig(void *dest, void *a, void *b, void *c)
{
   float fa = *(float *)a;
   int exp;
   float sig = frexpf(fa, &exp);
   // NIR frexp_sig returns the significand
   *(float *)dest = sig;
}

static void
gen_golden_bitfield_reverse(void *dest, void *a, void *b, void *c)
{
   uint32_t n = *(uint32_t *)a;
   n = ((n >> 1) & 0x55555555) | ((n & 0x55555555) << 1);
   n = ((n >> 2) & 0x33333333) | ((n & 0x33333333) << 2);
   n = ((n >> 4) & 0x0F0F0F0F) | ((n & 0x0F0F0F0F) << 4);
   n = ((n >> 8) & 0x00FF00FF) | ((n & 0x00FF00FF) << 8);
   n = ((n >> 16) & 0x0000FFFF) | ((n & 0x0000FFFF) << 16);
   *(uint32_t *)dest = n;
}

static void
gen_golden_bit_count(void *dest, void *a, void *b, void *c)
{
   uint32_t n = *(uint32_t *)a;
   *(uint32_t *)dest = (uint32_t)__builtin_popcount(n);
}

static inline void
store_bool(void *dest, bool result)
{
   *(uint32_t *)dest = result ? 1 : 0;
}

static void
gen_golden_ilt(void *dest, void *a, void *b, void *c)
{
   (void)c;
   int32_t ia = *(int32_t *)a;
   int32_t ib = *(int32_t *)b;
   store_bool(dest, ia < ib);
}

static void
gen_golden_ine(void *dest, void *a, void *b, void *c)
{
   (void)c;
   uint32_t ua = *(uint32_t *)a;
   uint32_t ub = *(uint32_t *)b;
   store_bool(dest, ua != ub);
}

static void
gen_golden_ieq(void *dest, void *a, void *b, void *c)
{
   (void)c;
   uint32_t ua = *(uint32_t *)a;
   uint32_t ub = *(uint32_t *)b;
   store_bool(dest, ua == ub);
}

static void
gen_golden_ige(void *dest, void *a, void *b, void *c)
{
   (void)c;
   int32_t ia = *(int32_t *)a;
   int32_t ib = *(int32_t *)b;
   store_bool(dest, ia >= ib);
}

static void
gen_golden_ult(void *dest, void *a, void *b, void *c)
{
   (void)c;
   uint32_t ua = *(uint32_t *)a;
   uint32_t ub = *(uint32_t *)b;
   store_bool(dest, ua < ub);
}

static void
gen_golden_uge(void *dest, void *a, void *b, void *c)
{
   (void)c;
   uint32_t ua = *(uint32_t *)a;
   uint32_t ub = *(uint32_t *)b;
   store_bool(dest, ua >= ub);
}

/* Ordered Equal: True if equal and neither is NaN */
static void
gen_golden_feq(void *dest, void *a, void *b, void *c)
{
   (void)c;
   float32_t fa = {*(uint32_t *)a};
   float32_t fb = {*(uint32_t *)b};
   store_bool(dest, f32_eq(fa, fb));
}

/* Unordered Not Equal: True if not equal OR either is NaN
f32_eq returns false for NaN, so !f32_eq returns true for NaN */
static void
gen_golden_fneu(void *dest, void *a, void *b, void *c)
{
   (void)c;
   float32_t fa = {*(uint32_t *)a};
   float32_t fb = {*(uint32_t *)b};
   store_bool(dest, !f32_eq(fa, fb));
}

/* Unordered Equal: True if equal OR either is NaN */
static void
gen_golden_fequ(void *dest, void *a, void *b, void *c)
{
   (void)c;
   float32_t fa = {*(uint32_t *)a};
   float32_t fb = {*(uint32_t *)b};
   bool res = f32_eq(fa, fb) || f32_isnan(fa) || f32_isnan(fb);
   store_bool(dest, res);
}

/* Ordered Not Equal: True if not equal AND neither is NaN */
static void
gen_golden_fneo(void *dest, void *a, void *b, void *c)
{
   (void)c;
   float32_t fa = {*(uint32_t *)a};
   float32_t fb = {*(uint32_t *)b};
   bool res = !f32_eq(fa, fb) && !f32_isnan(fa) && !f32_isnan(fb);
   store_bool(dest, res);
}

/* Ordered Less Than: False if NaN */
static void
gen_golden_flt(void *dest, void *a, void *b, void *c)
{
   (void)c;
   float32_t fa = {*(uint32_t *)a};
   float32_t fb = {*(uint32_t *)b};
   store_bool(dest, f32_lt(fa, fb));
}

/* Ordered Greater Equal: False if NaN
 f32_le(b, a) is equivalent to a >= b */
static void
gen_golden_fge(void *dest, void *a, void *b, void *c)
{
   (void)c;
   float32_t fa = {*(uint32_t *)a};
   float32_t fb = {*(uint32_t *)b};
   store_bool(dest, f32_le(fb, fa));
}

/* Unordered Less Than: True if a < b OR either is NaN
 If (b <= a) is False, then (a < b) or (unordered) is True. */
static void
gen_golden_fltu(void *dest, void *a, void *b, void *c)
{
   (void)c;
   float32_t fa = {*(uint32_t *)a};
   float32_t fb = {*(uint32_t *)b};
   store_bool(dest, !f32_le(fb, fa));
}

/* Unordered Greater Equal: True if a >= b OR either is NaN
 If (a < b) is False, then (a >= b) or (unordered) is True. */
static void
gen_golden_fgeu(void *dest, void *a, void *b, void *c)
{
   (void)c;
   float32_t fa = {*(uint32_t *)a};
   float32_t fb = {*(uint32_t *)b};
   store_bool(dest, !f32_lt(fa, fb));
}

static void
gen_golden_funord(void *dest, void *a, void *b, void *c)
{
   (void)c;
   float32_t fa = {*(uint32_t *)a};
   float32_t fb = {*(uint32_t *)b};
   store_bool(dest, f32_isnan(fa) || f32_isnan(fb));
}

static void
gen_golden_ford(void *dest, void *a, void *b, void *c)
{
   (void)c;
   float32_t fa = {*(uint32_t *)a};
   float32_t fb = {*(uint32_t *)b};
   store_bool(dest, !f32_isnan(fa) && !f32_isnan(fb));
}

static void
gen_golden_ffma(void *dest, void *a, void *b, void *c)
{
   float32_t fa = {*(uint32_t *)a};
   float32_t fb = {*(uint32_t *)b};
   float32_t fc = {*(uint32_t *)c};
   float32_t res = f32_mulAdd(fa, fb, fc);
   *(uint32_t *)dest = res.v;
}

/* Assuming standard fused-multiply-add behavior for ffmaz */
static void
gen_golden_ffmaz(void *dest, void *a, void *b, void *c)
{
   float32_t fa = {*(uint32_t *)a};
   float32_t fb = {*(uint32_t *)b};
   float32_t fc = {*(uint32_t *)c};
   float32_t res = f32_mulAdd(fa, fb, fc);
   *(uint32_t *)dest = res.v;
}

static void
gen_golden_bitfield_select(void *dest, void *a, void *b, void *c)
{
   uint32_t mask = *(uint32_t *)a;
   uint32_t v1 = *(uint32_t *)b;
   uint32_t v2 = *(uint32_t *)c;
   /* NIR: (src0 & src1) | (~src0 & src2) */
   *(uint32_t *)dest = (mask & v1) | (~mask & v2);
}

/* bitfield extract */
static void
gen_golden_ubfe(void *dest, void *a, void *b, void *c)
{
   uint32_t val = *(uint32_t *)a;
   uint32_t offset = *(uint32_t *)b;
   uint32_t bits = *(uint32_t *)c;

   offset &= 0x1f;
   bits &= 0x1f;

   if (bits == 0) {
      *(uint32_t *)dest = 0;
   } else if (offset + bits < 32) {
      *(uint32_t *)dest = (val << (32 - offset - bits)) >> (32 - bits);
   } else {
      *(uint32_t *)dest = val >> offset;
   }
}

/* signed bitfield extract */
static void
gen_golden_ibfe(void *dest, void *a, void *b, void *c)
{
   int32_t val = *(int32_t *)a;
   uint32_t offset = *(uint32_t *)b;
   uint32_t bits = *(uint32_t *)c;

   offset &= 0x1f;
   bits &= 0x1f;

   if (bits == 0) {
      *(uint32_t *)dest = 0;
   } else if (offset + bits < 32) {
      /* Shift left to clear high bits, shift right arithmetic to sign extend */
      uint32_t shift_left = 32 - (offset + bits);
      uint32_t shift_right = 32 - bits;
      int32_t res = val << shift_left;
      res = res >> shift_right;
      *(uint32_t *)dest = (uint32_t)res;
   } else {
      *(uint32_t *)dest = val >> offset;
   }
}

static void
gen_golden_sdot_4x8_iadd(void *dest, void *a, void *b, void *c)
{
   uint32_t va = *(uint32_t *)a;
   uint32_t vb = *(uint32_t *)b;
   int32_t vc = *(int32_t *)c;
   int32_t sum = 0;

   for (int i = 0; i < 4; i++) {
      sum += (int32_t)extract_i8(va, i) * (int32_t)extract_i8(vb, i);
   }
   *(uint32_t *)dest = (uint32_t)(sum + vc);
}

static void
gen_golden_sdot_4x8_iadd_sat(void *dest, void *a, void *b, void *c)
{
   uint32_t va = *(uint32_t *)a;
   uint32_t vb = *(uint32_t *)b;
   int32_t vc = *(int32_t *)c;
   int32_t sum = 0;

   for (int i = 0; i < 4; i++) {
      sum += (int32_t)extract_i8(va, i) * (int32_t)extract_i8(vb, i);
   }
   *(uint32_t *)dest = (uint32_t)add_sat_s32(sum, vc);
}

static void
gen_golden_sudot_4x8_iadd(void *dest, void *a, void *b, void *c)
{
   uint32_t va = *(uint32_t *)a;
   uint32_t vb = *(uint32_t *)b;
   int32_t vc = *(int32_t *)c;
   int32_t sum = 0;

   /* Mixed signed/unsigned dot: usually signed src0 * unsigned src1 */
   for (int i = 0; i < 4; i++) {
      sum += (int32_t)extract_i8(va, i) * (int32_t)extract_u8(vb, i);
   }
   *(uint32_t *)dest = (uint32_t)(sum + vc);
}

static void
gen_golden_sudot_4x8_iadd_sat(void *dest, void *a, void *b, void *c)
{
   uint32_t va = *(uint32_t *)a;
   uint32_t vb = *(uint32_t *)b;
   int32_t vc = *(int32_t *)c;
   int32_t sum = 0;

   for (int i = 0; i < 4; i++) {
      sum += (int32_t)extract_i8(va, i) * (int32_t)extract_u8(vb, i);
   }
   *(uint32_t *)dest = (uint32_t)add_sat_s32(sum, vc);
}

static void
gen_golden_udot_4x8_uadd(void *dest, void *a, void *b, void *c)
{
   uint32_t va = *(uint32_t *)a;
   uint32_t vb = *(uint32_t *)b;
   uint32_t vc = *(uint32_t *)c;
   uint32_t sum = 0;

   for (int i = 0; i < 4; i++) {
      sum += (uint32_t)extract_u8(va, i) * (uint32_t)extract_u8(vb, i);
   }
   *(uint32_t *)dest = sum + vc;
}

static void
gen_golden_udot_4x8_uadd_sat(void *dest, void *a, void *b, void *c)
{
   uint32_t va = *(uint32_t *)a;
   uint32_t vb = *(uint32_t *)b;
   uint32_t vc = *(uint32_t *)c;
   uint32_t sum = 0;

   for (int i = 0; i < 4; i++) {
      sum += (uint32_t)extract_u8(va, i) * (uint32_t)extract_u8(vb, i);
   }
   *(uint32_t *)dest = add_sat_u32(sum, vc);
}

static void
gen_golden_sdot_2x16_iadd(void *dest, void *a, void *b, void *c)
{
   uint32_t va = *(uint32_t *)a;
   uint32_t vb = *(uint32_t *)b;
   int32_t vc = *(int32_t *)c;
   int32_t sum = 0;

   for (int i = 0; i < 2; i++) {
      sum += (int32_t)extract_i16(va, i) * (int32_t)extract_i16(vb, i);
   }
   *(uint32_t *)dest = (uint32_t)(sum + vc);
}

static void
gen_golden_udot_2x16_uadd(void *dest, void *a, void *b, void *c)
{
   uint32_t va = *(uint32_t *)a;
   uint32_t vb = *(uint32_t *)b;
   uint32_t vc = *(uint32_t *)c;
   uint32_t sum = 0;

   for (int i = 0; i < 2; i++) {
      sum += (uint32_t)extract_u16(va, i) * (uint32_t)extract_u16(vb, i);
   }
   *(uint32_t *)dest = sum + vc;
}

static void
gen_golden_sdot_2x16_iadd_sat(void *dest, void *a, void *b, void *c)
{
   uint32_t va = *(uint32_t *)a;
   uint32_t vb = *(uint32_t *)b;
   int32_t vc = *(int32_t *)c;
   int32_t sum = 0;

   for (int i = 0; i < 2; i++) {
      sum += (int32_t)extract_i16(va, i) * (int32_t)extract_i16(vb, i);
   }
   *(uint32_t *)dest = (uint32_t)add_sat_s32(sum, vc);
}

static void
gen_golden_udot_2x16_uadd_sat(void *dest, void *a, void *b, void *c)
{
   uint32_t va = *(uint32_t *)a;
   uint32_t vb = *(uint32_t *)b;
   uint32_t vc = *(uint32_t *)c;
   uint32_t sum = 0;

   for (int i = 0; i < 2; i++) {
      sum += (uint32_t)extract_u16(va, i) * (uint32_t)extract_u16(vb, i);
   }
   *(uint32_t *)dest = add_sat_u32(sum, vc);
}

static void
gen_golden_shfr(void *dest, void *a, void *b, void *c)
{
   /* Funnel shift right: result = (a << (32-c)) | (b >> c)
      Assuming a=src0, b=src1, c=shift amount */
   uint32_t src0 = *(uint32_t *)a;
   uint32_t src1 = *(uint32_t *)b;
   uint32_t shift = (*(uint32_t *)c) & 0x1F;

   if (shift == 0) {
      *(uint32_t *)dest = src1;
   } else {
      /* Usually shfr (src0, src1, shift) acts as extracting from concatenated
       * (src0, src1) */
      *(uint32_t *)dest = (src0 << (32 - shift)) | (src1 >> shift);
   }
}

static void
gen_golden_msad_4x8(void *dest, void *a, void *b, void *c)
{
   /* Masked Sum of Absolute Differences: usually treated as SAD + Accumulate */
   uint32_t va = *(uint32_t *)a;
   uint32_t vb = *(uint32_t *)b;
   uint32_t vc = *(uint32_t *)c;
   uint32_t sum = 0;

   for (int i = 0; i < 4; i++) {
      uint8_t u_a = extract_u8(va, i);
      uint8_t u_b = extract_u8(vb, i);
      sum += (u_a > u_b) ? (u_a - u_b) : (u_b - u_a);
   }
   *(uint32_t *)dest = sum + vc;
}

static void
gen_golden_mqsad_4x8(void *dest, void *a, void *b, void *c)
{
   /* Not implemented */
}

struct fsa_nir_microbench_struct fsa_nir_benchlist[] = {
   /* --- Integer/Logic Binary Ops --- */
   ENTRY_GROUP(iadd, 2, gen_golden_iadd),
   ENTRY_GROUP(uadd_sat, 2, gen_golden_uadd_sat),
   ENTRY_GROUP(iadd_sat, 2, gen_golden_iadd_sat),
   ENTRY_GROUP(usub_sat, 2, gen_golden_usub_sat),
   ENTRY_GROUP(isub_sat, 2, gen_golden_isub_sat),
   ENTRY_GROUP(isub, 2, gen_golden_isub),
   ENTRY_GROUP(imul, 2, gen_golden_imul),
   ENTRY_GROUP(imul24_relaxed, 2, gen_golden_imul24_relaxed),
   ENTRY_GROUP(umul24_relaxed, 2, gen_golden_umul24_relaxed),
   ENTRY_GROUP(iand, 2, gen_golden_iand),
   ENTRY_GROUP(ior, 2, gen_golden_ior),
   ENTRY_GROUP(ixor, 2, gen_golden_ixor),
   ENTRY_GROUP(ishl, 2, gen_golden_ishl),
   ENTRY_GROUP(ishr, 2, gen_golden_ishr),
   ENTRY_GROUP(ushr, 2, gen_golden_ushr),
   ENTRY_GROUP(imax, 2, gen_golden_imax),
   ENTRY_GROUP(imin, 2, gen_golden_imin),
   ENTRY_GROUP(umax, 2, gen_golden_umax),
   ENTRY_GROUP(umin, 2, gen_golden_umin),
   ENTRY_GROUP(bfm, 2, gen_golden_bfm),

   /* --- Float Binary Ops --- */
   ENTRY_GROUP(fadd, 2, gen_golden_fadd),
   ENTRY_GROUP(fsub, 2, gen_golden_fsub),
   ENTRY_GROUP(fmul, 2, gen_golden_fmul),
   ENTRY_GROUP(fmulz, 2, gen_golden_fmulz),
   ENTRY_GROUP(fdiv, 2, gen_golden_fdiv),
   ENTRY_GROUP(fmax, 2, gen_golden_fmax),
   ENTRY_GROUP(fmin, 2, gen_golden_fmin),
   ENTRY_GROUP(ldexp, 2, gen_golden_ldexp),

   /* Ternary Operations */
   ENTRY_GROUP(ffma, 3, gen_golden_ffma),
   ENTRY_GROUP(ffmaz, 3, gen_golden_ffmaz),
   ENTRY_GROUP(bitfield_select, 3, gen_golden_bitfield_select),
   ENTRY_GROUP(ubfe, 3, gen_golden_ubfe),
   ENTRY_GROUP(ibfe, 3, gen_golden_ibfe),
   ENTRY_GROUP(sdot_4x8_iadd, 3, gen_golden_sdot_4x8_iadd),
   ENTRY_GROUP(sdot_4x8_iadd_sat, 3, gen_golden_sdot_4x8_iadd_sat),
   ENTRY_GROUP(sudot_4x8_iadd, 3, gen_golden_sudot_4x8_iadd),
   ENTRY_GROUP(sudot_4x8_iadd_sat, 3, gen_golden_sudot_4x8_iadd_sat),
   ENTRY_GROUP(udot_4x8_uadd, 3, gen_golden_udot_4x8_uadd),
   ENTRY_GROUP(udot_4x8_uadd_sat, 3, gen_golden_udot_4x8_uadd_sat),
   ENTRY_GROUP(sdot_2x16_iadd, 3, gen_golden_sdot_2x16_iadd),
   ENTRY_GROUP(udot_2x16_uadd, 3, gen_golden_udot_2x16_uadd),
   ENTRY_GROUP(sdot_2x16_iadd_sat, 3, gen_golden_sdot_2x16_iadd_sat),
   ENTRY_GROUP(udot_2x16_uadd_sat, 3, gen_golden_udot_2x16_uadd_sat),
   ENTRY_GROUP(shfr, 3, gen_golden_shfr),
   ENTRY_GROUP(msad_4x8, 3, gen_golden_msad_4x8),
   /* Cannot fit in current generation framework */
   // ENTRY_GROUP(mqsad_4x8, 3, gen_golden_mqsad_4x8),

   /* --- Unary Ops (Float & Int) --- */
   ENTRY_GROUP(fneg, 1, gen_golden_fneg),
   ENTRY_GROUP(inot, 1, gen_golden_inot),
   ENTRY_GROUP(frcp, 1, gen_golden_frcp),
   ENTRY_GROUP(fabs, 1, gen_golden_fabs),
   ENTRY_GROUP(fsat, 1, gen_golden_fsat),
   ENTRY_GROUP(iabs, 1, gen_golden_iabs),
   ENTRY_GROUP(isign, 1, gen_golden_isign),
   ENTRY_GROUP(fsign, 1, gen_golden_fsign),
   ENTRY_GROUP(ffloor, 1, gen_golden_ffloor),
   ENTRY_GROUP(ftrunc, 1, gen_golden_ftrunc),
   ENTRY_GROUP(fceil, 1, gen_golden_fceil),
   ENTRY_GROUP(fround_even, 1, gen_golden_fround_even),
   ENTRY_GROUP(ffract, 1, gen_golden_ffract),
   ENTRY_GROUP(fsqrt, 1, gen_golden_fsqrt),
   ENTRY_GROUP(fexp2, 1, gen_golden_fexp2),
   ENTRY_GROUP(flog2, 1, gen_golden_flog2),
   ENTRY_GROUP(frsq, 1, gen_golden_frsq),
   ENTRY_GROUP(frexp_exp, 1, gen_golden_frexp_exp),
   ENTRY_GROUP(frexp_sig, 1, gen_golden_frexp_sig),
   ENTRY_GROUP(bitfield_reverse, 1, gen_golden_bitfield_reverse),
   ENTRY_GROUP(bit_count, 1, gen_golden_bit_count),

   /* --- Comparisons (Binary Input -> Bool/Int Output) --- */
   ENTRY_GROUP(ilt, 2, gen_golden_ilt),
   ENTRY_GROUP(ine, 2, gen_golden_ine),
   ENTRY_GROUP(ieq, 2, gen_golden_ieq),
   ENTRY_GROUP(ige, 2, gen_golden_ige),
   ENTRY_GROUP(ult, 2, gen_golden_ult),
   ENTRY_GROUP(uge, 2, gen_golden_uge),
   ENTRY_GROUP(feq, 2, gen_golden_feq),
   ENTRY_GROUP(fneu, 2, gen_golden_fneu),
   ENTRY_GROUP(fequ, 2, gen_golden_fequ),
   ENTRY_GROUP(fneo, 2, gen_golden_fneo),
   ENTRY_GROUP(flt, 2, gen_golden_flt),
   ENTRY_GROUP(fge, 2, gen_golden_fge),
   ENTRY_GROUP(fltu, 2, gen_golden_fltu),
   ENTRY_GROUP(fgeu, 2, gen_golden_fgeu),
   ENTRY_GROUP(funord, 2, gen_golden_funord),
   ENTRY_GROUP(ford, 2, gen_golden_ford),
   {"if_scalar", "IF scalar using ilt", gen_if_scalar, 2, 1, gen_golden_if},
   {"if_phi_scalar", "IF + phi", gen_if_phi_scalar, 2, 1, gen_golden_if_phi},
   {"if_var_scalar", "IF + var", gen_if_var_scalar, 2, 1, gen_golden_if_phi},
   {"for_loop", "For loop", gen_loop, 1, 1, gen_golden_loop},
   /* End of list */
   {NULL, NULL, NULL, 0, 0},
};
