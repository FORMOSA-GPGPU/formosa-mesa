/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "fsa_compute.h"

static LLVMValueRef
byte_offset(struct fsa_llvm_context *ctx, LLVMValueRef base,
            LLVMValueRef offset)
{
   offset = LLVMBuildZExtOrBitCast(ctx->builder, offset, ctx->i64, "");
   return LLVMBuildGEP2(ctx->builder, ctx->i8, base, &offset, 1, "");
}

/* Offsets follow the firmware WGInfo ABI in cp_wgi_buffer.h. */
static LLVMValueRef
wg_load(struct fsa_llvm_context *ctx, unsigned offset, LLVMTypeRef type)
{
   LLVMValueRef base = fsa_build_info_ptr(ctx);
   LLVMValueRef ptr = byte_offset(ctx, base, LLVMConstInt(ctx->i64, offset, 0));
   return LLVMBuildLoad2(ctx->builder, type, ptr, "");
}

static LLVMValueRef
args_ptr(struct fsa_llvm_context *ctx)
{
   return wg_load(ctx, 0x40, ctx->ptrt);
}

static LLVMValueRef
buffer_member(struct fsa_llvm_context *ctx, LLVMValueRef index, unsigned member)
{
   LLVMValueRef offset =
      LLVMBuildMul(ctx->builder, index, LLVMConstInt(ctx->i32, 16, 0), "");
   offset = LLVMBuildAdd(ctx->builder, offset,
                         LLVMConstInt(ctx->i32, member * 8, 0), "");
   return LLVMBuildLoad2(ctx->builder, ctx->i64,
                         byte_offset(ctx, args_ptr(ctx), offset), "");
}

static LLVMValueRef
scratch_ptr(struct fsa_llvm_context *ctx)
{
   LLVMValueRef group = wg_load(ctx, 0xc, ctx->i32);
   for (int i = 1; i >= 0; i--) {
      group = LLVMBuildMul(ctx->builder, group,
                           wg_load(ctx, 0x1c + i * 4, ctx->i32), "");
      group = LLVMBuildAdd(ctx->builder, group,
                           wg_load(ctx, 4 + i * 4, ctx->i32), "");
   }
   LLVMValueRef threads = wg_load(ctx, 0x10, ctx->i32);
   threads =
      LLVMBuildMul(ctx->builder, threads, wg_load(ctx, 0x14, ctx->i32), "");
   threads =
      LLVMBuildMul(ctx->builder, threads, wg_load(ctx, 0x18, ctx->i32), "");
   LLVMValueRef index = LLVMBuildMul(
      ctx->builder, LLVMBuildZExt(ctx->builder, group, ctx->i64, ""),
      LLVMBuildZExt(ctx->builder, threads, ctx->i64, ""), "");
   index = LLVMBuildAdd(ctx->builder, index, fsa_build_local_id(ctx), "");
   LLVMValueRef stride = LLVMBuildLoad2(
      ctx->builder, ctx->i32,
      byte_offset(
         ctx, args_ptr(ctx),
         LLVMConstInt(ctx->i64,
                      offsetof(struct fsa_compute_args, scratch_stride), 0)),
      "");
   index = LLVMBuildMul(ctx->builder, index,
                        LLVMBuildZExt(ctx->builder, stride, ctx->i64, ""), "");
   LLVMValueRef ptr = LLVMBuildLoad2(
      ctx->builder, ctx->ptrt,
      byte_offset(
         ctx, args_ptr(ctx),
         LLVMConstInt(ctx->i64,
                      offsetof(struct fsa_compute_args, scratch_address), 0)),
      "");
   return byte_offset(ctx, ptr, index);
}

static LLVMValueRef
invocation_id(struct fsa_llvm_context *ctx, unsigned component)
{
   LLVMValueRef tid =
      LLVMBuildTrunc(ctx->builder, fsa_build_local_id(ctx), ctx->i32, "");
   LLVMValueRef x = wg_load(ctx, 0x10, ctx->i32);
   LLVMValueRef y = wg_load(ctx, 0x14, ctx->i32);
   if (component == 0)
      return LLVMBuildURem(ctx->builder, tid, x, "");
   tid = LLVMBuildUDiv(ctx->builder, tid, x, "");
   return component == 1 ? LLVMBuildURem(ctx->builder, tid, y, "")
                         : LLVMBuildUDiv(ctx->builder, tid, y, "");
}

static void
memory_fence(struct fsa_llvm_context *ctx, bool barrier)
{
   LLVMTypeRef type = LLVMFunctionType(ctx->voidt, NULL, 0, 0);
   char *code = "fence rw, rw";
   char constraints[] = "~{memory}";
   LLVMValueRef assembly = LLVMGetInlineAsm(
      type, code, strlen(code), constraints, strlen(constraints), true, false,
      LLVMInlineAsmDialectATT, false);
   LLVMBuildCall2(ctx->builder, type, assembly, NULL, 0, "");
   if (barrier) {
      const char *name = "llvm.riscv.fsa.barrier";
      unsigned id = LLVMLookupIntrinsicID(name, strlen(name));
      LLVMValueRef function =
         LLVMGetIntrinsicDeclaration(ctx->module, id, NULL, 0);
      LLVMValueRef args[] = {ctx->i64_0, ctx->i64_0};
      LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(function), function,
                     args, 2, "");
      LLVMBuildCall2(ctx->builder, type, assembly, NULL, 0, "");
   }
}

/* Masks change at divergent control flow and clocks advance during execution.
 * Unlike geometry CSRs, these reads cannot be merged, hoisted or duplicated. */
static LLVMValueRef
read_live_csr(struct fsa_llvm_context *ctx, const char *csr)
{
   LLVMTypeRef type = LLVMFunctionType(ctx->i64, NULL, 0, false);
   char code[64], constraints[] = "=r,~{memory}";
   snprintf(code, sizeof(code), "csrr $0, %s", csr);
   LLVMValueRef assembly = LLVMGetInlineAsm(
      type, code, strlen(code), constraints, strlen(constraints), true, false,
      LLVMInlineAsmDialectATT, false);
   LLVMValueRef call =
      LLVMBuildCall2(ctx->builder, type, assembly, NULL, 0, "");
   const char *attributes[] = {"convergent", "noduplicate"};
   for (unsigned i = 0; i < 2; i++)
      LLVMAddCallSiteAttribute(
         call, LLVMAttributeFunctionIndex,
         LLVMCreateEnumAttribute(ctx->context,
                                 LLVMGetEnumAttributeKindForName(
                                    attributes[i], strlen(attributes[i])),
                                 0));
   return call;
}

/* Coherent subword stores share a 32-bit AMO word with neighboring array
 * elements. A compare-exchange loop preserves simultaneous neighboring writes.
 * Keep the loop in a helper so NIR block/phi translation stays unchanged. */
static LLVMValueRef
store_subword(struct fsa_llvm_context *ctx, unsigned bits)
{
   const char *name = bits == 8 ? "fsa_store_i8" : "fsa_store_i16";
   LLVMValueRef function = LLVMGetNamedFunction(ctx->module, name);
   if (function)
      return function;
   LLVMTypeRef parameters[] = {ctx->ptrt,
                               LLVMIntTypeInContext(ctx->context, bits)};
   function = LLVMAddFunction(
      ctx->module, name, LLVMFunctionType(ctx->voidt, parameters, 2, false));
   LLVMSetLinkage(function, LLVMInternalLinkage);
   LLVMBuilderRef b = LLVMCreateBuilderInContext(ctx->context);
   LLVMBasicBlockRef entry =
      LLVMAppendBasicBlockInContext(ctx->context, function, "entry");
   LLVMBasicBlockRef loop =
      LLVMAppendBasicBlockInContext(ctx->context, function, "retry");
   LLVMBasicBlockRef done =
      LLVMAppendBasicBlockInContext(ctx->context, function, "done");
   LLVMPositionBuilderAtEnd(b, entry);
   LLVMValueRef address =
      LLVMBuildPtrToInt(b, LLVMGetParam(function, 0), ctx->i64, "");
   LLVMValueRef word = LLVMBuildIntToPtr(
      b,
      LLVMBuildAnd(b, address, LLVMConstInt(ctx->i64, ~UINT64_C(3), false), ""),
      ctx->ptrt, "");
   LLVMValueRef shift = LLVMBuildTrunc(
      b,
      LLVMBuildMul(
         b, LLVMBuildAnd(b, address, LLVMConstInt(ctx->i64, 3, false), ""),
         LLVMConstInt(ctx->i64, 8, false), ""),
      ctx->i32, "");
   LLVMValueRef mask = LLVMBuildShl(
      b, LLVMConstInt(ctx->i32, (1u << bits) - 1, false), shift, "");
   LLVMValueRef value = LLVMBuildShl(
      b, LLVMBuildZExt(b, LLVMGetParam(function, 1), ctx->i32, ""), shift, "");
   LLVMValueRef initial =
      LLVMBuildAtomicRMW(b, LLVMAtomicRMWBinOpOr, word, ctx->i32_0,
                         LLVMAtomicOrderingSequentiallyConsistent, false);
   LLVMBuildBr(b, loop);
   LLVMPositionBuilderAtEnd(b, loop);
   LLVMValueRef old = LLVMBuildPhi(b, ctx->i32, "");
   LLVMValueRef replacement = LLVMBuildOr(
      b, LLVMBuildAnd(b, old, LLVMBuildNot(b, mask, ""), ""), value, "");
   LLVMValueRef pair = LLVMBuildAtomicCmpXchg(
      b, word, old, replacement, LLVMAtomicOrderingSequentiallyConsistent,
      LLVMAtomicOrderingSequentiallyConsistent, false);
   LLVMValueRef observed = LLVMBuildExtractValue(b, pair, 0, "");
   LLVMValueRef incoming[] = {initial, observed};
   LLVMBasicBlockRef predecessors[] = {entry, loop};
   LLVMAddIncoming(old, incoming, predecessors, 2);
   LLVMBuildCondBr(b, LLVMBuildExtractValue(b, pair, 1, ""), done, loop);
   LLVMPositionBuilderAtEnd(b, done);
   LLVMBuildRetVoid(b);
   LLVMDisposeBuilder(b);
   return function;
}

static bool
emit_atomic(struct fsa_llvm_context *ctx, nir_intrinsic_instr *instr,
            LLVMValueRef ptr, LLVMValueRef data, LLVMValueRef compare,
            LLVMValueRef *result)
{
   if (instr->def.bit_size != 32 && instr->def.bit_size != 64)
      return false;
   LLVMAtomicRMWBinOp op;
   switch (nir_intrinsic_atomic_op(instr)) {
   case nir_atomic_op_iadd:
      op = LLVMAtomicRMWBinOpAdd;
      break;
   case nir_atomic_op_iand:
      op = LLVMAtomicRMWBinOpAnd;
      break;
   case nir_atomic_op_ior:
      op = LLVMAtomicRMWBinOpOr;
      break;
   case nir_atomic_op_ixor:
      op = LLVMAtomicRMWBinOpXor;
      break;
   case nir_atomic_op_imin:
      op = LLVMAtomicRMWBinOpMin;
      break;
   case nir_atomic_op_imax:
      op = LLVMAtomicRMWBinOpMax;
      break;
   case nir_atomic_op_umin:
      op = LLVMAtomicRMWBinOpUMin;
      break;
   case nir_atomic_op_umax:
      op = LLVMAtomicRMWBinOpUMax;
      break;
   case nir_atomic_op_xchg:
      op = LLVMAtomicRMWBinOpXchg;
      break;
   case nir_atomic_op_cmpxchg: {
      LLVMValueRef pair = LLVMBuildAtomicCmpXchg(
         ctx->builder, ptr, data, compare,
         LLVMAtomicOrderingSequentiallyConsistent,
         LLVMAtomicOrderingSequentiallyConsistent, false);
      *result = LLVMBuildExtractValue(ctx->builder, pair, 0, "");
      return true;
   }
   default:
      return false;
   }
   *result =
      LLVMBuildAtomicRMW(ctx->builder, op, ptr, data,
                         LLVMAtomicOrderingSequentiallyConsistent, false);
   return true;
}

static LLVMValueRef
resource_index(struct fsa_llvm_context *ctx, unsigned set, unsigned binding,
               LLVMValueRef element)
{
   if (set >= FSA_COMPUTE_MAX_SETS || binding >= FSA_COMPUTE_MAX_BINDINGS)
      return NULL;
   unsigned offset = offsetof(struct fsa_compute_args, bindings) +
                     (set * FSA_COMPUTE_MAX_BINDINGS + binding) * 4;
   LLVMValueRef base = LLVMBuildLoad2(
      ctx->builder, ctx->i32,
      byte_offset(ctx, args_ptr(ctx), LLVMConstInt(ctx->i64, offset, 0)), "");
   return element ? LLVMBuildAdd(ctx->builder, base, element, "") : base;
}

LLVMValueRef
fsa_compute_emit_deref(struct fsa_llvm_context *ctx, nir_deref_instr *deref,
                       LLVMValueRef parent, LLVMValueRef index)
{
   if (deref->deref_type == nir_deref_type_var) {
      nir_variable *var = deref->var;
      if (!(var->data.mode & (nir_var_image | nir_var_uniform)))
         return NULL;
      return resource_index(ctx, var->data.descriptor_set, var->data.binding,
                            NULL);
   }
   if (deref->deref_type != nir_deref_type_array)
      return NULL;
   unsigned stride =
      glsl_type_is_array(deref->type) ? glsl_get_aoa_size(deref->type) : 1;
   index = LLVMBuildTruncOrBitCast(ctx->builder, index, ctx->i32, "");
   index =
      LLVMBuildMul(ctx->builder, index, LLVMConstInt(ctx->i32, stride, 0), "");
   return LLVMBuildAdd(ctx->builder, parent, index, "");
}

static LLVMValueRef
image_ptr(struct fsa_llvm_context *ctx, LLVMValueRef index)
{
   LLVMValueRef offset = LLVMBuildMul(
      ctx->builder, index, LLVMConstInt(ctx->i32, sizeof(dev_image_t), 0), "");
   offset = LLVMBuildAdd(
      ctx->builder, offset,
      LLVMConstInt(ctx->i32, offsetof(struct fsa_compute_args, images), 0), "");
   return byte_offset(ctx, args_ptr(ctx), offset);
}

static LLVMValueRef
image_size(struct fsa_llvm_context *ctx, LLVMValueRef index,
           enum glsl_sampler_dim dim, bool array, unsigned components)
{
   LLVMValueRef values[4], ptr = image_ptr(ctx, index);
   for (unsigned i = 0; i < components; i++) {
      unsigned offset = offsetof(dev_image_t, _width) + 4 * i;
      if (array && i == components - 1)
         offset = offsetof(dev_image_t, _image_array_size);
      values[i] = LLVMBuildLoad2(
         ctx->builder, ctx->i32,
         byte_offset(ctx, ptr, LLVMConstInt(ctx->i64, offset, 0)), "");
   }
   return components == 1 ? values[0]
                          : fsa_build_gather_values(ctx, values, components);
}

static LLVMValueRef
image_pixel_pointer(struct fsa_llvm_context *ctx, LLVMValueRef index,
                    enum glsl_sampler_dim dim, bool array, LLVMValueRef coord)
{
   LLVMValueRef descriptor = image_ptr(ctx, index);
   LLVMValueRef data = LLVMBuildLoad2(ctx->builder, ctx->ptrt, descriptor, "");
   LLVMValueRef x = fsa_llvm_extract_elem(ctx, coord, 0);
   LLVMValueRef offset =
      LLVMBuildMul(ctx->builder, x, LLVMConstInt(ctx->i32, 4, 0), "");
   if (dim == GLSL_SAMPLER_DIM_2D || dim == GLSL_SAMPLER_DIM_3D) {
      LLVMValueRef row = LLVMBuildLoad2(
         ctx->builder, ctx->i32,
         byte_offset(
            ctx, descriptor,
            LLVMConstInt(ctx->i64, offsetof(dev_image_t, _row_pitch), 0)),
         "");
      offset = LLVMBuildAdd(
         ctx->builder, offset,
         LLVMBuildMul(ctx->builder, fsa_llvm_extract_elem(ctx, coord, 1), row,
                      ""),
         "");
   }
   if (array || dim == GLSL_SAMPLER_DIM_3D) {
      LLVMValueRef slice = LLVMBuildLoad2(
         ctx->builder, ctx->i32,
         byte_offset(
            ctx, descriptor,
            LLVMConstInt(ctx->i64, offsetof(dev_image_t, _slice_pitch), 0)),
         "");
      unsigned component = dim == GLSL_SAMPLER_DIM_1D ? 1 : 2;
      offset = LLVMBuildAdd(
         ctx->builder, offset,
         LLVMBuildMul(ctx->builder,
                      fsa_llvm_extract_elem(ctx, coord, component), slice, ""),
         "");
   }
   return byte_offset(ctx, data, offset);
}

/* Reuse PoCL's native RISC-V image builtins, including its format conversion
 * and sampler implementation. Their vector C ABI is i64/i128 under lp64. */
static LLVMValueRef
image_call(struct fsa_llvm_context *ctx, LLVMValueRef index,
           enum glsl_sampler_dim dim, bool array, nir_alu_type type,
           LLVMValueRef coord, LLVMValueRef sampler, LLVMValueRef value)
{
   if (dim != GLSL_SAMPLER_DIM_1D && dim != GLSL_SAMPLER_DIM_2D &&
       dim != GLSL_SAMPLER_DIM_3D && dim != GLSL_SAMPLER_DIM_BUF)
      return NULL;
   const char *shape =
      dim == GLSL_SAMPLER_DIM_BUF  ? "image1d_buffer"
      : dim == GLSL_SAMPLER_DIM_1D ? (array ? "image1d_array" : "image1d")
      : dim == GLSL_SAMPLER_DIM_2D ? (array ? "image2d_array" : "image2d")
                                   : "image3d";
   unsigned count =
      dim == GLSL_SAMPLER_DIM_1D && !array ? 1
      : dim == GLSL_SAMPLER_DIM_BUF        ? 1
      : (dim == GLSL_SAMPLER_DIM_2D && !array) || dim == GLSL_SAMPLER_DIM_1D
         ? 2
         : 4;
   bool floating_coord = sampler != NULL;
   LLVMTypeRef component = floating_coord ? ctx->f32 : ctx->i32;
   LLVMValueRef coords[4];
   unsigned provided = LLVMGetTypeKind(LLVMTypeOf(coord)) == LLVMVectorTypeKind
                          ? LLVMGetVectorSize(LLVMTypeOf(coord))
                          : 1;
   for (unsigned i = 0; i < count; i++) {
      coords[i] =
         i < provided
            ? (provided == 1 ? coord : fsa_llvm_extract_elem(ctx, coord, i))
            : ctx->i32_0;
      if (floating_coord)
         coords[i] = LLVMBuildBitCast(ctx->builder, coords[i], component, "");
   }
   coord = count == 1 ? coords[0] : fsa_build_gather_values(ctx, coords, count);
   LLVMTypeRef coord_type =
      count == 1 ? component : LLVMIntTypeInContext(ctx->context, count * 32);
   coord = LLVMBuildBitCast(ctx->builder, coord, coord_type, "");
   const char *suffix = nir_alu_type_get_base_type(type) == nir_type_float ? "f"
                        : nir_alu_type_get_base_type(type) == nir_type_uint
                           ? "ui"
                           : "i";
   char function[192], image_type[64], coordinate[16], operation[32];
   snprintf(image_type, sizeof(image_type), "ocl_%s_%s", shape,
            value ? "wo" : "ro");
   snprintf(operation, sizeof(operation), "_cl_%s_image%s",
            value ? "write" : "read", suffix);
   snprintf(coordinate, sizeof(coordinate), "%s", floating_coord ? "f" : "i");
   if (count != 1)
      snprintf(coordinate, sizeof(coordinate), "Dv%u_%s", count,
               floating_coord ? "f" : "i");
   snprintf(function, sizeof(function), "_Z%zu%s%zu%s%s%s%s%s",
            strlen(operation), operation, strlen(image_type), image_type,
            sampler ? "11ocl_sampler" : "", coordinate, value ? "Dv4_" : "",
            value ? (strcmp(suffix, "ui") == 0 ? "j" : suffix) : "");
   LLVMTypeRef types[4] = {ctx->ptrt};
   LLVMValueRef params[4] = {image_ptr(ctx, index)};
   unsigned n = 1;
   if (sampler) {
      LLVMValueRef offset = LLVMBuildAdd(
         ctx->builder,
         LLVMBuildMul(ctx->builder, sampler, LLVMConstInt(ctx->i32, 4, 0), ""),
         LLVMConstInt(ctx->i32, offsetof(struct fsa_compute_args, samplers), 0),
         "");
      LLVMValueRef bits = LLVMBuildLoad2(
         ctx->builder, ctx->i32, byte_offset(ctx, args_ptr(ctx), offset), "");
      types[n] = ctx->ptrt;
      params[n++] = LLVMBuildIntToPtr(
         ctx->builder, LLVMBuildZExt(ctx->builder, bits, ctx->i64, ""),
         ctx->ptrt, "");
   }
   types[n] = coord_type;
   params[n++] = coord;
   LLVMTypeRef i128 = LLVMIntTypeInContext(ctx->context, 128);
   if (value) {
      types[n] = i128;
      params[n++] = LLVMBuildBitCast(ctx->builder, value, i128, "");
   }
   LLVMTypeRef ftype =
      LLVMFunctionType(value ? ctx->voidt : i128, types, n, false);
   LLVMValueRef f = LLVMGetNamedFunction(ctx->module, function);
   if (!f)
      f = LLVMAddFunction(ctx->module, function, ftype);
   LLVMValueRef call = LLVMBuildCall2(ctx->builder, ftype, f, params, n, "");
   return value ? call : LLVMBuildBitCast(ctx->builder, call, ctx->v4i32, "");
}

bool
fsa_compute_emit_tex(struct fsa_llvm_context *ctx, nir_tex_instr *tex,
                     LLVMValueRef *src, LLVMValueRef *result)
{
   int image = nir_tex_instr_src_index(tex, nir_tex_src_texture_deref);
   if (image < 0)
      return false;
   if (tex->op == nir_texop_txs) {
      *result = image_size(ctx, src[image], tex->sampler_dim, tex->is_array,
                           tex->def.num_components);
      return true;
   }
   int lod = nir_tex_instr_src_index(tex, nir_tex_src_lod);
   if (lod >= 0 && (!nir_src_is_const(tex->src[lod].src) ||
                    (tex->op == nir_texop_txf
                        ? nir_src_as_int(tex->src[lod].src) != 0
                        : nir_src_as_float(tex->src[lod].src) != 0.0f)))
      return false;
   int coord = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   int sampler = nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref);
   if (coord < 0 || (tex->op != nir_texop_txl && tex->op != nir_texop_txf))
      return false;
   *result = image_call(
      ctx, src[image], tex->sampler_dim, tex->is_array, tex->dest_type,
      src[coord],
      tex->op == nir_texop_txl && sampler >= 0 ? src[sampler] : NULL, NULL);
   return *result != NULL;
}

bool
fsa_compute_emit_intrinsic(struct fsa_llvm_context *ctx,
                           nir_intrinsic_instr *instr, LLVMValueRef *src,
                           LLVMValueRef *result)
{
   LLVMValueRef ptr = NULL;
   unsigned offset_src = 0;
   bool store = false;
   switch (instr->intrinsic) {
   case nir_intrinsic_image_deref_atomic:
   case nir_intrinsic_image_deref_atomic_swap: {
      nir_variable *image =
         nir_deref_instr_get_variable(nir_src_as_deref(instr->src[0]));
      if (!image || (image->data.image.format != PIPE_FORMAT_R32_UINT &&
                     image->data.image.format != PIPE_FORMAT_R32_SINT))
         return false;
      ptr = image_pixel_pointer(ctx, src[0], nir_intrinsic_image_dim(instr),
                                nir_intrinsic_image_array(instr), src[1]);
      return emit_atomic(ctx, instr, ptr, src[3], src[4], result);
   }
   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_store: {
      nir_variable *image =
         nir_deref_instr_get_variable(nir_src_as_deref(instr->src[0]));
      enum pipe_format format =
         image ? image->data.image.format : PIPE_FORMAT_NONE;
      if (format == PIPE_FORMAT_R32_UINT || format == PIPE_FORMAT_R32_SINT ||
          format == PIPE_FORMAT_R32_FLOAT) {
         /* Use the same coherent L2 path as image atomics. Ordinary cached
          * accesses can otherwise hide imageStore from imageAtomicAdd. */
         ptr = image_pixel_pointer(ctx, src[0], nir_intrinsic_image_dim(instr),
                                   nir_intrinsic_image_array(instr), src[1]);
         if (instr->intrinsic == nir_intrinsic_image_deref_store) {
            LLVMBuildAtomicRMW(ctx->builder, LLVMAtomicRMWBinOpXchg, ptr,
                               fsa_llvm_extract_elem(ctx, src[3], 0),
                               LLVMAtomicOrderingSequentiallyConsistent, false);
         } else {
            LLVMValueRef values[] = {
               LLVMBuildAtomicRMW(
                  ctx->builder, LLVMAtomicRMWBinOpOr, ptr, ctx->i32_0,
                  LLVMAtomicOrderingSequentiallyConsistent, false),
               ctx->i32_0, ctx->i32_0,
               LLVMConstInt(ctx->i32,
                            format == PIPE_FORMAT_R32_FLOAT ? 0x3f800000 : 1,
                            false)};
            *result =
               fsa_build_gather_values(ctx, values, instr->def.num_components);
         }
         return true;
      }
      if (instr->intrinsic == nir_intrinsic_image_deref_load) {
         *result =
            image_call(ctx, src[0], nir_intrinsic_image_dim(instr),
                       nir_intrinsic_image_array(instr),
                       nir_intrinsic_dest_type(instr), src[1], NULL, NULL);
         return *result != NULL;
      }
      return image_call(ctx, src[0], nir_intrinsic_image_dim(instr),
                        nir_intrinsic_image_array(instr),
                        nir_intrinsic_src_type(instr), src[1], NULL,
                        src[3]) != NULL;
   }
   case nir_intrinsic_image_deref_size:
      *result = image_size(ctx, src[0], nir_intrinsic_image_dim(instr),
                           nir_intrinsic_image_array(instr),
                           instr->def.num_components);
      return true;
   case nir_intrinsic_image_deref_levels:
   case nir_intrinsic_image_deref_samples:
      *result = ctx->i32_1;
      return true;
   case nir_intrinsic_load_printf_buffer_address:
      *result = LLVMBuildLoad2(
         ctx->builder, ctx->i64,
         byte_offset(
            ctx, args_ptr(ctx),
            LLVMConstInt(ctx->i64,
                         offsetof(struct fsa_compute_args, printf_address), 0)),
         "");
      return true;
   case nir_intrinsic_load_printf_buffer_size:
      *result = LLVMConstInt(ctx->i32, FSA_COMPUTE_PRINTF_SIZE, 0);
      return true;
   case nir_intrinsic_global_atomic:
   case nir_intrinsic_global_atomic_swap:
      ptr = LLVMBuildIntToPtr(ctx->builder, src[0], ctx->ptrt, "");
      return emit_atomic(ctx, instr, ptr, src[1], src[2], result);
   case nir_intrinsic_load_scratch:
   case nir_intrinsic_store_scratch:
      ptr = scratch_ptr(ctx);
      store = instr->intrinsic == nir_intrinsic_store_scratch;
      offset_src = store ? 1 : 0;
      break;
   case nir_intrinsic_load_global:
      ptr = LLVMBuildIntToPtr(ctx->builder, src[0], ctx->ptrt, "");
      offset_src = UINT32_MAX;
      break;
   case nir_intrinsic_store_global:
      ptr = LLVMBuildIntToPtr(ctx->builder, src[1], ctx->ptrt, "");
      offset_src = UINT32_MAX;
      store = true;
      break;
   case nir_intrinsic_elect: {
      LLVMValueRef mask = read_live_csr(ctx, "0xfc2");
      LLVMValueRef lane = fsa_build_csr_read(ctx, "xlaneid");
      LLVMValueRef lower = LLVMBuildSub(
         ctx->builder,
         LLVMBuildShl(ctx->builder, LLVMConstInt(ctx->i64, 1, false), lane, ""),
         LLVMConstInt(ctx->i64, 1, false), "");
      *result = LLVMBuildICmp(ctx->builder, LLVMIntEQ,
                              LLVMBuildAnd(ctx->builder, mask, lower, ""),
                              ctx->i64_0, "");
      return true;
   }
   case nir_intrinsic_shader_clock:
      *result = LLVMBuildBitCast(ctx->builder, read_live_csr(ctx, "cycle"),
                                 ctx->v2i32, "");
      return true;
   case nir_intrinsic_load_subgroup_size:
   case nir_intrinsic_load_subgroup_invocation:
   case nir_intrinsic_load_subgroup_id:
   case nir_intrinsic_load_num_subgroups: {
      LLVMValueRef width = LLVMBuildTrunc(
         ctx->builder, fsa_build_csr_read(ctx, "xlanes"), ctx->i32, "");
      LLVMValueRef tid =
         LLVMBuildTrunc(ctx->builder, fsa_build_local_id(ctx), ctx->i32, "");
      if (instr->intrinsic == nir_intrinsic_load_subgroup_size)
         *result = width;
      else if (instr->intrinsic == nir_intrinsic_load_subgroup_invocation)
         *result = LLVMBuildURem(ctx->builder, tid, width, "");
      else if (instr->intrinsic == nir_intrinsic_load_subgroup_id)
         *result = LLVMBuildUDiv(ctx->builder, tid, width, "");
      else {
         LLVMValueRef count = wg_load(ctx, 0x10, ctx->i32);
         count =
            LLVMBuildMul(ctx->builder, count, wg_load(ctx, 0x14, ctx->i32), "");
         count =
            LLVMBuildMul(ctx->builder, count, wg_load(ctx, 0x18, ctx->i32), "");
         count =
            LLVMBuildAdd(ctx->builder, count,
                         LLVMBuildSub(ctx->builder, width, ctx->i32_1, ""), "");
         *result = LLVMBuildUDiv(ctx->builder, count, width, "");
      }
      return true;
   }
   case nir_intrinsic_load_base_global_invocation_id:
   case nir_intrinsic_load_base_workgroup_id: {
      LLVMValueRef values[3];
      for (unsigned i = 0; i < 3; i++) {
         LLVMValueRef ptr = byte_offset(
            ctx, args_ptr(ctx),
            LLVMConstInt(ctx->i64,
                         offsetof(struct fsa_compute_args, base_group) + i * 4,
                         0));
         values[i] = LLVMBuildLoad2(ctx->builder, ctx->i32, ptr, "");
         if (instr->intrinsic == nir_intrinsic_load_base_global_invocation_id)
            values[i] = LLVMBuildMul(ctx->builder, values[i],
                                     wg_load(ctx, 0x10 + i * 4, ctx->i32), "");
      }
      *result = fsa_build_gather_values(ctx, values, 3);
      return true;
   }
   case nir_intrinsic_load_local_invocation_id:
   case nir_intrinsic_load_workgroup_id:
   case nir_intrinsic_load_num_workgroups:
   case nir_intrinsic_load_workgroup_size:
   case nir_intrinsic_load_global_invocation_id: {
      LLVMValueRef values[3];
      for (unsigned i = 0; i < 3; i++) {
         switch (instr->intrinsic) {
         case nir_intrinsic_load_local_invocation_id:
            values[i] = invocation_id(ctx, i);
            break;
         case nir_intrinsic_load_global_invocation_id:
            values[i] = LLVMBuildAdd(
               ctx->builder, invocation_id(ctx, i),
               LLVMBuildMul(ctx->builder, wg_load(ctx, 0x10 + i * 4, ctx->i32),
                            wg_load(ctx, 0x4 + i * 4, ctx->i32),
                            ""),
               "");
            break;
         default: {
            unsigned base =
               instr->intrinsic == nir_intrinsic_load_workgroup_id     ? 4
               : instr->intrinsic == nir_intrinsic_load_num_workgroups ? 0x1c
                                                                       : 0x10;
            values[i] = wg_load(ctx, base + i * 4, ctx->i32);
            break;
         }
         }
      }
      *result = fsa_build_gather_values(ctx, values, 3);
      return true;
   }
   case nir_intrinsic_load_local_invocation_index:
      *result =
         LLVMBuildTrunc(ctx->builder, fsa_build_local_id(ctx), ctx->i32, "");
      return true;
   case nir_intrinsic_vulkan_resource_index: {
      if (instr->def.num_components != 2)
         return false;
      LLVMValueRef index = resource_index(ctx, nir_intrinsic_desc_set(instr),
                                          nir_intrinsic_binding(instr), src[0]);
      if (!index)
         return false;
      LLVMValueRef values[] = {index, ctx->i32_0};
      *result = fsa_build_gather_values(ctx, values, instr->def.num_components);
      return true;
   }
   case nir_intrinsic_vulkan_resource_reindex: {
      LLVMValueRef index = fsa_llvm_extract_elem(ctx, src[0], 0);
      LLVMValueRef values[] = {LLVMBuildAdd(ctx->builder, index, src[1], ""),
                               ctx->i32_0};
      *result = fsa_build_gather_values(ctx, values, 2);
      return true;
   }
   case nir_intrinsic_load_vulkan_descriptor:
      *result = src[0];
      return true;
   case nir_intrinsic_get_ssbo_size:
      *result = LLVMBuildTrunc(ctx->builder, buffer_member(ctx, src[0], 1),
                               ctx->i32, "");
      return true;
   case nir_intrinsic_load_push_constant:
      ptr = byte_offset(ctx, args_ptr(ctx),
                        LLVMConstInt(ctx->i64,
                                     offsetof(struct fsa_compute_args, push) +
                                        nir_intrinsic_base(instr),
                                     0));
      break;
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ssbo:
      ptr = LLVMBuildIntToPtr(ctx->builder, buffer_member(ctx, src[0], 0),
                              ctx->ptrt, "");
      offset_src = 1;
      break;
   case nir_intrinsic_store_ssbo:
      ptr = LLVMBuildIntToPtr(ctx->builder, buffer_member(ctx, src[1], 0),
                              ctx->ptrt, "");
      offset_src = 2;
      store = true;
      break;
   case nir_intrinsic_load_shared:
   case nir_intrinsic_store_shared:
      ptr = wg_load(ctx, 0x58, ctx->ptrt);
      ptr = byte_offset(ctx, ptr,
                        LLVMConstInt(ctx->i64, nir_intrinsic_base(instr), 0));
      store = instr->intrinsic == nir_intrinsic_store_shared;
      offset_src = store ? 1 : 0;
      break;
   case nir_intrinsic_ssbo_atomic:
   case nir_intrinsic_ssbo_atomic_swap:
      ptr = LLVMBuildIntToPtr(ctx->builder, buffer_member(ctx, src[0], 0),
                              ctx->ptrt, "");
      ptr = byte_offset(ctx, ptr, src[1]);
      return emit_atomic(ctx, instr, ptr, src[2], src[3], result);
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap: {
      LLVMValueRef offset = LLVMBuildAdd(ctx->builder, src[0],
         LLVMConstInt(ctx->i32, nir_intrinsic_base(instr), 0), "");
      ptr = byte_offset(ctx, wg_load(ctx, 0x58, ctx->ptrt), offset);
      return emit_atomic(ctx, instr, ptr, src[1], src[2], result);
   }
   case nir_intrinsic_barrier:
      if (nir_intrinsic_execution_scope(instr) != SCOPE_NONE &&
          nir_intrinsic_execution_scope(instr) != SCOPE_WORKGROUP &&
          nir_intrinsic_execution_scope(instr) != SCOPE_SUBGROUP)
         return false;
      memory_fence(ctx,
                   nir_intrinsic_execution_scope(instr) == SCOPE_WORKGROUP);
      return true;
   default:
      fprintf(stderr, "Formosa compute: unsupported intrinsic %s\n",
              nir_intrinsic_infos[instr->intrinsic].name);
      return false;
   }

   if (offset_src != UINT32_MAX)
      ptr = byte_offset(ctx, ptr, src[offset_src]);
   bool coherent = instr->intrinsic == nir_intrinsic_store_global ||
                   instr->intrinsic == nir_intrinsic_load_global ||
                   (nir_intrinsic_has_access(instr) &&
                    (nir_intrinsic_access(instr) & ACCESS_COHERENT));
   if (store) {
      unsigned components = instr->src[0].ssa->num_components;
      unsigned bytes = instr->src[0].ssa->bit_size / 8;
      for (unsigned i = 0; i < components; i++) {
         if (!(nir_intrinsic_write_mask(instr) & (1u << i)))
            continue;
         LLVMValueRef value =
            components == 1 ? src[0] : fsa_llvm_extract_elem(ctx, src[0], i);
         LLVMValueRef dest =
            byte_offset(ctx, ptr, LLVMConstInt(ctx->i64, i * bytes, 0));
         if (coherent) {
            if (bytes == 1 || bytes == 2) {
               LLVMValueRef function = store_subword(ctx, bytes * 8);
               LLVMValueRef values[] = {dest, value};
               LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(function),
                              function, values, 2, "");
               continue;
            }
            if (bytes != 4 && bytes != 8)
               return false;
            LLVMBuildAtomicRMW(ctx->builder, LLVMAtomicRMWBinOpXchg, dest,
                               value, LLVMAtomicOrderingSequentiallyConsistent,
                               false);
         } else {
            LLVMValueRef op = LLVMBuildStore(ctx->builder, value, dest);
            LLVMSetAlignment(op, bytes);
         }
      }
   } else {
      LLVMTypeRef type =
         LLVMIntTypeInContext(ctx->context, instr->def.bit_size);
      if (instr->def.num_components > 1)
         type = LLVMVectorType(type, instr->def.num_components);
      if (coherent) {
         unsigned bytes = instr->def.bit_size / 8;
         if (bytes != 1 && bytes != 2 && bytes != 4 && bytes != 8)
            return false;
         LLVMValueRef values[16];
         for (unsigned i = 0; i < instr->def.num_components; i++) {
            LLVMValueRef address = LLVMBuildPtrToInt(
               ctx->builder,
               byte_offset(ctx, ptr, LLVMConstInt(ctx->i64, i * bytes, false)),
               ctx->i64, "");
            LLVMValueRef dest =
               bytes < 4
                  ? LLVMBuildIntToPtr(
                       ctx->builder,
                       LLVMBuildAnd(ctx->builder, address,
                                    LLVMConstInt(ctx->i64, ~UINT64_C(3), false),
                                    ""),
                       ctx->ptrt, "")
                  : LLVMBuildIntToPtr(ctx->builder, address, ctx->ptrt, "");
            values[i] = LLVMBuildAtomicRMW(
               ctx->builder, LLVMAtomicRMWBinOpOr, dest,
               LLVMConstInt(LLVMIntTypeInContext(ctx->context,
                                                 MAX2(32, instr->def.bit_size)),
                            0, false),
               LLVMAtomicOrderingSequentiallyConsistent, false);
            if (bytes < 4) {
               LLVMValueRef shift = LLVMBuildTrunc(
                  ctx->builder,
                  LLVMBuildMul(
                     ctx->builder,
                     LLVMBuildAnd(ctx->builder, address,
                                  LLVMConstInt(ctx->i64, 3, false), ""),
                     LLVMConstInt(ctx->i64, 8, false), ""),
                  ctx->i32, "");
               values[i] = LLVMBuildTrunc(
                  ctx->builder,
                  LLVMBuildLShr(ctx->builder, values[i], shift, ""),
                  LLVMIntTypeInContext(ctx->context, instr->def.bit_size), "");
            }
         }
         *result = instr->def.num_components == 1
                      ? values[0]
                      : fsa_build_gather_values(ctx, values,
                                                instr->def.num_components);
      } else {
         *result = LLVMBuildLoad2(ctx->builder, type, ptr, "");
         LLVMSetAlignment(*result, instr->def.bit_size / 8);
      }
   }
   return true;
}
