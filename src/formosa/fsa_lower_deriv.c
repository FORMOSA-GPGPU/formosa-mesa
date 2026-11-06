#include "fsa_lower_deriv.h"
#include "nir.h"
#include "nir_builder.h"

static nir_def *
get_subgroup_invocation(nir_builder *b)
{
   return nir_load_subgroup_invocation(b);
}

static bool
lower_ddx_ddy_filter(const nir_instr *instr, const void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   switch (intrin->intrinsic) {
   case nir_intrinsic_ddx:
   case nir_intrinsic_ddx_fine:
   case nir_intrinsic_ddx_coarse:
   case nir_intrinsic_ddy:
   case nir_intrinsic_ddy_fine:
   case nir_intrinsic_ddy_coarse:
      return true;
   default:
      return false;
   }
}

static nir_def *
lower_ddx_ddy_impl(nir_builder *b, nir_instr *instr, void *data)
{
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   nir_def *src = intrin->src[0].ssa;

   /* Swap horizontal: Lane 0 <-> Lane 1, Lane 2 <-> Lane 3 */
   nir_def *h_swap = nir_quad_swap_horizontal(b, src);
   nir_def *h_diff = nir_fsub(b, h_swap, src);

   /* Fix sign based on lane ID */
   nir_def *lane_id = get_subgroup_invocation(b);
   nir_def *is_odd_x = nir_iand_imm(b, lane_id, 1);
   nir_def *h_sign = nir_bcsel(b, nir_ieq_imm(b, is_odd_x, 0),
                               nir_imm_float(b, 1.0), nir_imm_float(b, -1.0));
   nir_def *ddx = nir_fmul(b, h_diff, h_sign);

   /* Swap vertical: Lane 0 <-> Lane 2, Lane 1 <-> Lane 3 */
   nir_def *v_swap = nir_quad_swap_vertical(b, src);
   nir_def *v_diff = nir_fsub(b, v_swap, src);

   /* Fix sign based on lane ID */
   nir_def *is_odd_y = nir_iand_imm(b, lane_id, 2);
   nir_def *v_sign = nir_bcsel(b, nir_ieq_imm(b, is_odd_y, 0),
                               nir_imm_float(b, 1.0), nir_imm_float(b, -1.0));

   nir_def *ddy = nir_fmul(b, v_diff, v_sign);

   switch (intrin->intrinsic) {
   case nir_intrinsic_ddx:
   case nir_intrinsic_ddx_fine:
   case nir_intrinsic_ddx_coarse:
      return ddx;
   case nir_intrinsic_ddy:
   case nir_intrinsic_ddy_fine:
   case nir_intrinsic_ddy_coarse:
      /* May need to negate the result if texture coord origin  is bottom left */
      return ddy;
   default:
      return NULL;
   }
}

bool
fsa_lower_deriv(nir_shader *shader)
{
   return nir_shader_lower_instructions(shader, lower_ddx_ddy_filter,
                                        lower_ddx_ddy_impl, NULL);
}