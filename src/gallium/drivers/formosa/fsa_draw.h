#ifndef FSA_DRAW_H
#define FSA_DRAW_H

#include "pipe/p_context.h"
#include "pipe/p_state.h"

void fsa_draw_vbo(struct pipe_context *pctx, const struct pipe_draw_info *info,
                  unsigned drawid_offset,
                  const struct pipe_draw_indirect_info *indirect,
                  const struct pipe_draw_start_count_bias *draws,
                  unsigned num_draws);

#endif