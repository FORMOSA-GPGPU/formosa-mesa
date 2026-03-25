#include "fsa_lower_to_submit.h"

#include "nir.h"
#include "nir_builder.h"

/* This pass adds fsa specific intrinsic that submits fragment at the end of the
 * shader. It takes value from color and depth from store_output. This pass must
 * run after nir_lower_io_to_temporary to ensure only one write to color and one
 * write to depth in the shader. This pass must run before interpolation code
 * patching in able to get the interpolated frag coord for depth */

static nir_intrinsic_instr *
find_store_to_color(nir_block *last_bb)
{
   nir_foreach_instr_reverse_safe (instr, last_bb) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      if (intrin->intrinsic != nir_intrinsic_store_output)
         continue;
      nir_io_semantics sem = nir_intrinsic_io_semantics(intrin);
      if (sem.location == FRAG_RESULT_DATA0 ||
          sem.location == FRAG_RESULT_COLOR)
         return intrin;
   }
   return NULL;
}

static nir_intrinsic_instr *
find_store_to_depth(nir_block *last_bb)
{
   nir_foreach_instr_reverse_safe (instr, last_bb) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      if (intrin->intrinsic != nir_intrinsic_store_output)
         continue;
      nir_io_semantics sem = nir_intrinsic_io_semantics(intrin);
      if (sem.location == FRAG_RESULT_DEPTH)
         return intrin;
   }
   return NULL;
}

bool
fsa_nir_lower_to_submit(nir_shader *shader)
{
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   nir_builder b = nir_builder_create(impl);
   nir_block *last_bb = nir_impl_last_block(impl);

   b.cursor = nir_after_block(last_bb);

   /* Find color, depth store instr */
   nir_intrinsic_instr *store_color = find_store_to_color(last_bb);
   nir_intrinsic_instr *store_depth = find_store_to_depth(last_bb);

   nir_def *i32_color;
   if (store_color) {
      /* Lower color source (vec4) into packed uint32_t. The values stored to
       * color is in range 0.0 to 1.0, we must first map to 0-255 and convert to
       * integer */
      b.cursor = nir_before_instr(&store_color->instr);
      nir_def *v4f_color = store_color->src[0].ssa;
      nir_def *v4f_clamped = nir_fsat(&b, v4f_color);
      nir_def *v4f_scaled = nir_fmul_imm(&b, v4f_clamped, 255.0f);
      nir_def *v4f_rounded = nir_fround_even(&b, v4f_scaled);
      nir_def *v4i32_color = nir_f2u32(&b, v4f_rounded);
      nir_def *v4i8_color = nir_u2u8(&b, v4i32_color);
      /* Before swizzle, the vector format is RGBA. We must convert to BGRA */
      unsigned swiz[4] = {2, 1, 0, 3};
      nir_def *v4i8_color_swiz = nir_swizzle(&b, v4i8_color, swiz, 4);
      /* nir_pack then makes it ARGB in uint32_t */
      i32_color = nir_pack_32_4x8(&b, v4i8_color_swiz);

      nir_instr_remove((nir_instr *)store_color);
   } else {
      /* No write to color, default to zero (all black) */
      i32_color = nir_imm_int(&b, 0);
   }

   nir_def *depth_val;
   if (store_depth) {
      depth_val = store_depth->src[0].ssa;
      nir_instr_remove((nir_instr *)store_depth);
   } else {
      /* No write to depth, default to value from gl_Fragcoord z value */
      nir_def *frag_coord = nir_load_frag_coord(&b);
      depth_val = nir_channel(&b, frag_coord, 2);

      shader->info.inputs_read |= VARYING_SLOT_POS;
   }

   b.cursor = nir_after_block(last_bb);
   /* Load screen space position x, y then pack into 32-bit */
   nir_def *pos_x = nir_load_frag_coord_x_fsa(&b, 32);
   nir_def *pos_y = nir_load_frag_coord_y_fsa(&b, 32);
   nir_def *i16_pos_x = nir_u2u16(&b, pos_x);
   nir_def *i16_pos_y = nir_u2u16(&b, pos_y);
   /* Notice nir_pack format */
   nir_def *v2i16_pos = nir_vec2(&b, i16_pos_y, i16_pos_x);
   nir_def *packed_pos = nir_pack_32_2x16(&b, v2i16_pos);

   /* Insert the new rop instruction */
   nir_submit_pixel_fsa(&b, packed_pos, depth_val, i32_color);

   nir_metadata_preserve(impl,
                         nir_metadata_block_index | nir_metadata_dominance);
   return true;
}
