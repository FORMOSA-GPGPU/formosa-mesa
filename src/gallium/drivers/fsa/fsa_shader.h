#ifndef FSA_SHADER_H
#define FSA_SHADER_H

#include "pipe/p_context.h"
#include "pipe/p_state.h"

#include "uscsim/cbind.h"

struct fsa_shader_state {
   struct pipe_shader_state base;
   void *compiled_bin;
   size_t bin_size;
   USCSimKernel *kernel;

   uint64_t active_varying_slot;
   int varying_slot_stride[32];
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
void fsa_bind_vs_state(struct pipe_context *pctx, void *hwcso);
void fsa_delete_vs_state(struct pipe_context *pctx, void *hwcso);
void fsa_bind_fs_state(struct pipe_context *pctx, void *hwcso);
void fsa_delete_fs_state(struct pipe_context *pctx, void *hwcso);
#endif