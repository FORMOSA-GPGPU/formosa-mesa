#include "fsa_draw.h"
#include "fsa_debug.h"

#include "pipe/p_context.h"
#include "pipe/p_state.h"

void
fsa_draw_vbo(struct pipe_context *pctx, const struct pipe_draw_info *info,
             unsigned drawid_offset,
             const struct pipe_draw_indirect_info *indirect,
             const struct pipe_draw_start_count_bias *draws, unsigned num_draws)
{
   FSA_DBG(FSA_DEBUG_DRAW, "fsa_draw_vbo\n");
}