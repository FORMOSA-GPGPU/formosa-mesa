#include "fsa_lower_viewport_to_ubo.h"
#include "nir/nir.h"
#include "nir/nir_builder.h"

static bool
lower_viewport_to_ubo(nir_builder *b, nir_intrinsic_instr *instr, void *data)
{
   if (instr->intrinsic != nir_intrinsic_load_viewport_scale &&
       instr->intrinsic != nir_intrinsic_load_viewport_offset)
      return false;

   b->cursor = nir_before_instr(&instr->instr);

   int idx = instr->intrinsic == nir_intrinsic_load_viewport_scale ? 16 : 17;
   nir_def *ubo_idx = nir_imm_int(b, idx);
   nir_def *offset = nir_imm_int(b, 0);

   nir_def *load =
      nir_load_ubo(b, 3, 32, ubo_idx, offset, .align_mul = 8, .align_offset = 0,
                   .range_base = 0, .range = ~0);

   nir_def_rewrite_uses(&instr->def, load);
   nir_instr_remove((nir_instr *)instr);

   return true;
}

bool
fsa_lower_viewport_to_ubo(nir_shader *shader)
{
   if (shader->info.stage != MESA_SHADER_VERTEX)
      return false;

   return nir_shader_intrinsics_pass(shader, lower_viewport_to_ubo,
                                     nir_metadata_control_flow, NULL);
}