#include "fsa_lower_viewport_transform.h"
#include "nir/nir.h"
#include "nir/nir_builder.h"

/* Modified from `nir_lower_viewport_transform`. The linked shaders already have
 * `store_deref` lowered to `store_output`. I don't know how panfrost work, so
 * I'm writing a version that looks for `store_output` to VARYING_SLOT_POS */

/* Vulkan spec requires float accuracy to ~10^-5, so we need to differentiate
 * between W=0 and W=10^-5. Clamping 1/W to 2^15 gives a bit of margin on top
 * of this. */
#define MAX_W_RECIP_ABS (float)(1l << 15)

static bool
lower_viewport_transform_instr(nir_builder *b, nir_intrinsic_instr *intr,
                               void *data)
{
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   if (sem.location != VARYING_SLOT_POS) {
      return false;
   }

   b->cursor = nir_before_instr(&intr->instr);

   /* Grab the source and viewport */
   nir_def *input_point = intr->src[0].ssa;
   nir_def *scale = nir_load_viewport_scale(b);
   nir_def *offset = nir_load_viewport_offset(b);

   /* World space to normalised device coordinates to screen space */

   nir_def *w_recip = nir_frcp(b, nir_channel(b, input_point, 3));

   /* Clamp w_recip so that small w does not result in Inf or unacceptable
    * precision loss relative to clipping pre-transform. */
   w_recip = nir_fclamp(b, w_recip, nir_imm_float(b, -MAX_W_RECIP_ABS),
                        nir_imm_float(b, MAX_W_RECIP_ABS));

   nir_def *ndc_point =
      nir_fmul(b, nir_trim_vector(b, input_point, 3), w_recip);

   nir_def *screen = nir_fadd(b, nir_fmul(b, ndc_point, scale), offset);

   /* gl_Position will be written out in screenspace xyz, with w set to
    * the reciprocal we computed earlier. The transformed w component is
    * then used for perspective-correct varying interpolation. The
    * transformed w component must preserve its original sign; this is
    * used in depth clipping computations
    */

   nir_def *screen_space =
      nir_vec4(b, nir_channel(b, screen, 0), nir_channel(b, screen, 1),
               nir_channel(b, screen, 2), w_recip);

   nir_src_rewrite(&intr->src[0], screen_space);
   return true;
}

bool
fsa_lower_viewport_transform(nir_shader *shader)
{
   assert(shader->info.stage == MESA_SHADER_VERTEX);

   return nir_shader_intrinsics_pass(shader, lower_viewport_transform_instr,
                                     nir_metadata_control_flow, NULL);
}