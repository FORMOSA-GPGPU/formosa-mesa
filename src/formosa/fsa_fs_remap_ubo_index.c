#include "fsa_fs_remap_ubo_index.h"
#include <assert.h>
#include "nir.h"
#include "nir_builder.h"

bool
fsa_fs_remap_ubo_index(nir_shader *shader)
{
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);
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
            if (nir_src_is_const(*block_index_src) &&
                nir_src_as_uint(*block_index_src) == 0) {

               b.cursor = nir_before_instr(instr);

               /* Change to hardcoded 1 for fs */
               nir_def *new_index_val = nir_imm_int(&b, 1);
               nir_src_rewrite(block_index_src, new_index_val);
               progress = true;
            }
         }
      }
      nir_metadata_preserve(function->impl,
                            nir_metadata_block_index | nir_metadata_dominance);
   }
   return progress;
}