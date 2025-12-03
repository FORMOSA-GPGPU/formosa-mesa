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
   case nir_op_fadd:
      src[0] = fsa_to_float(&ctx->fsa, src[0]);
      src[1] = fsa_to_float(&ctx->fsa, src[1]);
      result = LLVMBuildFAdd(ctx->fsa.builder, src[0], src[1], "");
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
      ctx->ssa_defs[instr->def.index] = result;
   }
   return true;
}

static LLVMValueRef
get_pointer_val(struct fsa_llvm_context *ctx, LLVMTypeRef elem_type,
                LLVMValueRef value)
{
   LLVMTypeRef ptr_type = LLVMPointerType(elem_type, 0);
   LLVMValueRef input_ptr =
      LLVMBuildIntToPtr(ctx->builder, value, ptr_type, "");
   return input_ptr;
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

   /* Scalar */
   if (LLVMGetTypeKind(dest_type) != LLVMVectorTypeKind) {
      LLVMValueRef input_addr = LLVMConstInt(ctx->fsa.i64, 87654321, 0);
      LLVMValueRef input_ptr =
         get_pointer_val(&ctx->fsa, dest_type, input_addr);
      return LLVMBuildLoad2(ctx->fsa.builder, dest_type, input_ptr, "");
   }

   LLVMValueRef result = LLVMGetUndef(dest_type);
   LLVMTypeRef elem_type = LLVMGetElementType(dest_type);
   if (ctx->stage == MESA_SHADER_VERTEX) {
      for (unsigned chan = 0; chan < count; chan++) {
         /* Fake input address, TODO */
         LLVMValueRef input_addr =
            LLVMConstInt(ctx->fsa.i64, 0x87654300 + chan * 8, 0);
         LLVMValueRef input_ptr =
            get_pointer_val(&ctx->fsa, elem_type, input_addr);
         LLVMValueRef scalar_result =
            LLVMBuildLoad2(ctx->fsa.builder, elem_type, input_ptr, "");
         LLVMValueRef idx = LLVMConstInt(ctx->fsa.i32, chan, 0);
         result = LLVMBuildInsertElement(ctx->fsa.builder, result,
                                         scalar_result, idx, "");
      }
   } else if (ctx->stage == MESA_SHADER_FRAGMENT) {
      for (unsigned chan = 0; chan < count; chan++) {
         LLVMValueRef input_addr =
            LLVMConstInt(ctx->fsa.i64, 0x87654300 + chan * 8, 0);
         LLVMValueRef input_ptr =
            get_pointer_val(&ctx->fsa, elem_type, input_addr);
         LLVMValueRef scalar_result =
            LLVMBuildLoad2(ctx->fsa.builder, elem_type, input_ptr, "");
         LLVMValueRef idx = LLVMConstInt(ctx->fsa.i32, chan, 0);
         result = LLVMBuildInsertElement(ctx->fsa.builder, result,
                                         scalar_result, idx, "");
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
         get_pointer_val(&ctx->fsa, elem_type, output_addr);
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

   visit_cf_list(ctx, &func->impl->body);

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
      visit_function(&ctx, func);
      _mesa_hash_table_destroy(ctx.defs, NULL);
   }

   _mesa_hash_table_destroy(ctx.func_map, NULL);
   return true;
}