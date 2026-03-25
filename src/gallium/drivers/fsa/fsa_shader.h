#ifndef FSA_SHADER_H
#define FSA_SHADER_H

#include "pipe/p_context.h"
#include "pipe/p_state.h"

struct fsa_shader_state {
   struct pipe_shader_state base;
   void *compiled_bin;
   size_t bin_size;
};

static inline struct fsa_shader_state *
fsa_shader_state(void *state)
{
   return (struct fsa_shader_state *)state;
}

void *fsa_create_vs_state(struct pipe_context *pctx,
                          const struct pipe_shader_state *templat);

void *fsa_create_fs_state(struct pipe_context *pctx,
                          const struct pipe_shader_state *templat);

#endif