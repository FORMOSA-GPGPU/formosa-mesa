#include "fsa_lower_implicit_lod_tex.h"
#include "nir.h"
#include "nir_builder.h"

/* Copied from `nir_lower_tex.c` */
/* tex(s, coord) = txd(s, coord, dfdx(coord), dfdy(coord)) */
static nir_tex_instr *
lower_tex_to_txd(nir_builder *b, nir_tex_instr *tex)
{
   b->cursor = nir_after_instr(&tex->instr);
   nir_tex_instr *txd = nir_tex_instr_create(b->shader, tex->num_srcs + 2);

   txd->op = nir_texop_txd;
   txd->sampler_dim = tex->sampler_dim;
   txd->dest_type = tex->dest_type;
   txd->coord_components = tex->coord_components;
   txd->texture_index = tex->texture_index;
   txd->sampler_index = tex->sampler_index;
   txd->is_array = tex->is_array;
   txd->is_shadow = tex->is_shadow;
   txd->is_new_style_shadow = tex->is_new_style_shadow;

   /* reuse existing srcs */
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      txd->src[i].src = nir_src_for_ssa(tex->src[i].src.ssa);
      txd->src[i].src_type = tex->src[i].src_type;
   }
   int coord_idx = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   assert(coord_idx >= 0);
   nir_def *coord = tex->src[coord_idx].src.ssa;
   /* don't take the derivative of the array index */
   if (tex->is_array)
      coord =
         nir_channels(b, coord, nir_component_mask(coord->num_components - 1));
   nir_def *dfdx = nir_ddx(b, coord);
   nir_def *dfdy = nir_ddy(b, coord);
   txd->src[tex->num_srcs] = nir_tex_src_for_ssa(nir_tex_src_ddx, dfdx);
   txd->src[tex->num_srcs + 1] = nir_tex_src_for_ssa(nir_tex_src_ddy, dfdy);

   nir_def_init(&txd->instr, &txd->def, tex->def.num_components,
                tex->def.bit_size);
   nir_builder_instr_insert(b, &txd->instr);
   nir_def_replace(&tex->def, &txd->def);
   return txd;
}

static bool
lower_implicit_to_explicit_impl(nir_function_impl *impl)
{
   bool progress = false;
   nir_builder b = nir_builder_create(impl);

   nir_foreach_block (block, impl) {
      nir_foreach_instr_safe (instr, block) {
         if (instr->type != nir_instr_type_tex)
            continue;

         nir_tex_instr *tex = nir_instr_as_tex(instr);

         /* Only convert implicit tex */
         if (tex->op != nir_texop_tex)
            continue;

         lower_tex_to_txd(&b, tex);
         progress = true;
      }
   }

   if (progress) {
      nir_metadata_preserve(impl,
                            nir_metadata_block_index | nir_metadata_dominance);
   } else {
      nir_metadata_preserve(impl, nir_metadata_all);
   }

   return progress;
}

bool
fsa_lower_implicit_lod_tex(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function_impl (impl, shader) {
      progress |= lower_implicit_to_explicit_impl(impl);
   }

   return progress;
}