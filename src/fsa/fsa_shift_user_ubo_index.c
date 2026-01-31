#include "fsa_shift_user_ubo_index.h"

#include <assert.h>
#include "nir.h"
#include "nir_builder.h"

/**
 * Shifts all user-defined UBO indices by 'shamt'
 * Explicitly ignores the Default Uniform Block (Index 0)
 */
bool
fsa_shift_user_ubo_index(nir_shader *shader, uint32_t shamt)
{
   bool progress = false;

   nir_foreach_function (function, shader) {
      if (!function->impl)
         continue;

      nir_builder b = nir_builder_create(function->impl);

      nir_foreach_block (block, function->impl) {
         nir_foreach_instr (instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic != nir_intrinsic_load_ubo)
               continue;

            nir_src *block_index_src = &intrin->src[0];

            /* Ignore default ubo */
            if (nir_src_is_const(*block_index_src) &&
                nir_src_as_uint(*block_index_src) == 0) {
               continue;
            }

            /* This is a user UBO (either a constant > 0, or a dynamic index) */
            b.cursor = nir_before_instr(instr);
            nir_def *shift_val = nir_imm_int(&b, shamt);
            nir_def *old_index = block_index_src->ssa;
            nir_def *new_index = nir_iadd(&b, old_index, shift_val);
            nir_src_rewrite(block_index_src, new_index);
            progress = true;
         }
      }

      if (progress) {
         nir_metadata_preserve(
            function->impl, nir_metadata_block_index | nir_metadata_dominance);
      }
   }
   return progress;
}