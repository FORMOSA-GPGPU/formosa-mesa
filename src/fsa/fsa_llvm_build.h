#ifndef FSA_LLVM_BUILD_H
#define FSA_LLVM_BUILD_H

#include <stdbool.h>
#include <llvm-c/Core.h>

struct fsa_llvm_flow;
struct fsa_llvm_flow_state {
   struct fsa_llvm_flow *stack;
   unsigned depth_max;
   unsigned depth;
};

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
   LLVMTypeRef v2i8;
   LLVMTypeRef v3i8;
   LLVMTypeRef v4i8;
   LLVMTypeRef v2i16;
   LLVMTypeRef v3i16;
   LLVMTypeRef v4i16;
   LLVMTypeRef v2i32;
   LLVMTypeRef v3i32;
   LLVMTypeRef v4i32;
   LLVMTypeRef v2f32;
   LLVMTypeRef v3f32;
   LLVMTypeRef v4f32;

   LLVMTypeRef ptrt;
   LLVMTypeRef gfx_info_t;
   LLVMTypeRef gfx_state_t;
   LLVMTypeRef gfx_layout_t;

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

   struct fsa_llvm_flow_state *flow;

   /* Flags to determine compilation for spike or uscsim */
   bool is_cpu;
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
LLVMValueRef fsa_build_load_input_to_scalar(struct fsa_llvm_context *ctx,
                                            LLVMValueRef addr,
                                            LLVMValueRef scalar);
LLVMValueRef fsa_build_load_input_to_vector(struct fsa_llvm_context *ctx,
                                            LLVMValueRef addr, LLVMValueRef vec,
                                            LLVMValueRef idx);
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
LLVMValueRef fsa_get_pointer_val(struct fsa_llvm_context *ctx,
                                 LLVMTypeRef elem_type, LLVMValueRef addr);
void fsa_build_type_name_for_intr(LLVMTypeRef type, char *buf,
                                  unsigned bufsize);
LLVMValueRef fsa_build_const_int_vec(struct fsa_llvm_context *ctx,
                                     LLVMTypeRef type, long long val,
                                     bool sign_extend);
LLVMValueRef fsa_const_uint_vec(struct fsa_llvm_context *ctx, LLVMTypeRef type,
                                uint64_t value);
LLVMValueRef fsa_build_floating_reciprocal(struct fsa_llvm_context *ctx,
                                           LLVMValueRef value);
LLVMValueRef fsa_build_fmin(struct fsa_llvm_context *ctx, LLVMValueRef a,
                            LLVMValueRef b);
LLVMValueRef fsa_build_fmax(struct fsa_llvm_context *ctx, LLVMValueRef a,
                            LLVMValueRef b);
LLVMValueRef fsa_build_imin(struct fsa_llvm_context *ctx, LLVMValueRef a,
                            LLVMValueRef b);
LLVMValueRef fsa_build_imax(struct fsa_llvm_context *ctx, LLVMValueRef a,
                            LLVMValueRef b);
LLVMValueRef fsa_build_umin(struct fsa_llvm_context *ctx, LLVMValueRef a,
                            LLVMValueRef b);
LLVMValueRef fsa_build_umax(struct fsa_llvm_context *ctx, LLVMValueRef a,
                            LLVMValueRef b);
LLVMValueRef fsa_build_clamp(struct fsa_llvm_context *ctx, LLVMValueRef value);
LLVMValueRef fsa_build_fsat(struct fsa_llvm_context *ctx, LLVMValueRef src,
                            LLVMTypeRef type);
LLVMValueRef fsa_build_isign(struct fsa_llvm_context *ctx, LLVMValueRef src0);
LLVMValueRef fsa_build_fsign(struct fsa_llvm_context *ctx, LLVMValueRef src);
LLVMValueRef fsa_build_ffract(struct fsa_llvm_context *ctx, LLVMValueRef src);
LLVMValueRef fsa_build_frexp_exp(struct fsa_llvm_context *ctx,
                                 LLVMValueRef src0, unsigned bitsize);
LLVMValueRef fsa_build_frexp_mant(struct fsa_llvm_context *ctx,
                                  LLVMValueRef src0, unsigned bitsize);
LLVMValueRef fsa_build_bfe(struct fsa_llvm_context *ctx, LLVMValueRef input,
                           LLVMValueRef offset, LLVMValueRef width,
                           bool is_signed);
LLVMValueRef fsa_build_bitfield_reverse(struct fsa_llvm_context *ctx,
                                        LLVMValueRef src0);
LLVMValueRef fsa_build_bit_count(struct fsa_llvm_context *ctx,
                                 LLVMValueRef src0);
LLVMValueRef fsa_build_ldexp(struct fsa_llvm_context *ctx, LLVMValueRef src0,
                             LLVMValueRef src1, unsigned bitsize);
LLVMValueRef fsa_find_lsb(struct fsa_llvm_context *ctx, LLVMTypeRef dst_type,
                          LLVMValueRef src0);
LLVMValueRef ac_build_umsb(struct fsa_llvm_context *ctx, LLVMValueRef arg,
                           LLVMTypeRef dst_type, bool rev);
LLVMValueRef fsa_build_imsb(struct fsa_llvm_context *ctx, LLVMValueRef arg,
                            LLVMTypeRef dst_type, bool rev);
LLVMValueRef fsa_build_cvt_pknorm_i16(struct fsa_llvm_context *ctx,
                                      LLVMValueRef args[2]);
LLVMValueRef fsa_build_cvt_pknorm_u16(struct fsa_llvm_context *ctx,
                                      LLVMValueRef args[2]);
LLVMValueRef fsa_build_cvt_pknorm_i16_f16(struct fsa_llvm_context *ctx,
                                          LLVMValueRef args[2]);
LLVMValueRef fsa_build_cvt_pknorm_u16_f16(struct fsa_llvm_context *ctx,
                                          LLVMValueRef args[2]);
LLVMValueRef fsa_build_pack_uint_2x16(struct fsa_llvm_context *ctx,
                                      LLVMValueRef args[2]);
LLVMValueRef fsa_build_pack_sint_2x16(struct fsa_llvm_context *ctx,
                                      LLVMValueRef args[2]);
LLVMValueRef fsa_build_sdot_4x8_iadd(struct fsa_llvm_context *ctx,
                                     LLVMValueRef args[3]);
LLVMValueRef fsa_build_sdot_4x8_iadd_sat(struct fsa_llvm_context *ctx,
                                         LLVMValueRef args[3]);
LLVMValueRef fsa_build_sudot_4x8_iadd(struct fsa_llvm_context *ctx,
                                      LLVMValueRef args[3]);
LLVMValueRef fsa_build_sudot_4x8_iadd_sat(struct fsa_llvm_context *ctx,
                                          LLVMValueRef args[3]);
LLVMValueRef fsa_build_udot_4x8_uadd(struct fsa_llvm_context *ctx,
                                     LLVMValueRef args[3]);
LLVMValueRef fsa_build_udot_4x8_uadd_sat(struct fsa_llvm_context *ctx,
                                         LLVMValueRef args[3]);
LLVMValueRef fsa_build_sdot_2x16_iadd(struct fsa_llvm_context *ctx,
                                      LLVMValueRef args[3]);
LLVMValueRef fsa_build_udot_2x16_uadd(struct fsa_llvm_context *ctx,
                                      LLVMValueRef args[3]);
LLVMValueRef fsa_build_sdot_2x16_iadd_sat(struct fsa_llvm_context *ctx,
                                          LLVMValueRef args[3]);
LLVMValueRef fsa_build_udot_2x16_uadd_sat(struct fsa_llvm_context *ctx,
                                          LLVMValueRef args[3]);
LLVMValueRef fsa_build_msad_4x8(struct fsa_llvm_context *ctx,
                                LLVMValueRef args[3]);
LLVMValueRef fsa_build_mqsad_4x8(struct fsa_llvm_context *ctx,
                                 LLVMValueRef args[3]);
void fsa_build_ifcc(struct fsa_llvm_context *ctx, LLVMValueRef cond,
                    int label_id);
void fsa_build_else(struct fsa_llvm_context *ctx, int label_id);
void fsa_build_endif(struct fsa_llvm_context *ctx, int label_id);
void fsa_build_bgnloop(struct fsa_llvm_context *ctx, int label_id);
void fsa_build_endloop(struct fsa_llvm_context *ctx, int label_id);
void fsa_build_break(struct fsa_llvm_context *ctx);
void fsa_build_continue(struct fsa_llvm_context *ctx);
LLVMValueRef fsa_build_csr_read(struct fsa_llvm_context *ctx,
                                const char *csr_name);
LLVMValueRef fsa_build_local_id(struct fsa_llvm_context *ctx);
LLVMValueRef fsa_build_global_id(struct fsa_llvm_context *ctx);
LLVMValueRef fsa_build_info_ptr(struct fsa_llvm_context *ctx);
LLVMValueRef fsa_build_get_gfx_info_member_ptr(struct fsa_llvm_context *ctx,
                                               int idx);

typedef enum {
   fsa_vert_attrib = 0,
   fsa_vary_slot_pos,
   fsa_vary_slot_tex,
   fsa_vary_slot_var,
   fsa_frag_result,
} fsa_gfx_state_member;

LLVMValueRef fsa_build_get_gfx_state_layout_ptr(struct fsa_llvm_context *ctx,
                                                fsa_gfx_state_member member,
                                                int idx);
LLVMValueRef fsa_build_get_gfx_layout_base(struct fsa_llvm_context *ctx,
                                           fsa_gfx_state_member member,
                                           int idx);
LLVMValueRef fsa_build_get_gfx_layout_stride(struct fsa_llvm_context *ctx,
                                             fsa_gfx_state_member member,
                                             int idx);

#endif