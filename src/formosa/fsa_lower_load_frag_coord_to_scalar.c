#include "fsa_lower_load_frag_coord_to_scalar.h"

#include "nir.h"
#include "nir_builder.h"

static bool
lower_frag_coord_to_scalar_instr(nir_builder *b, nir_instr *intr, void *data)
{
   if (intr->type != nir_instr_type_intrinsic)
      return false;
   nir_intrinsic_instr *instr = nir_instr_as_intrinsic(intr);
   if (instr->intrinsic != nir_intrinsic_load_frag_coord)
      return false;
   b->cursor = nir_before_instr(&instr->instr);
   nir_def *x = nir_load_frag_coord_x_fsa(b, 32);
   nir_def *y = nir_load_frag_coord_y_fsa(b, 32);
   nir_def *z = nir_load_frag_coord_z_fsa(b, 32);
   nir_def *w = nir_load_frag_coord_w_fsa(b, 32);
   nir_def *vec = nir_vec4(b, x, y, z, w);
   nir_def_rewrite_uses(&instr->def, vec);
   nir_instr_remove((nir_instr *)instr);
   return true;
}

bool
fsa_lower_load_frag_coord_to_scalar(nir_shader *shader)
{
   return nir_shader_instructions_pass(
      shader, lower_frag_coord_to_scalar_instr,
      nir_metadata_block_index | nir_metadata_dominance, NULL);
}