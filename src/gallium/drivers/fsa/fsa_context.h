#ifndef FSA_CONTEXT_H
#define FSA_CONTEXT_H

#include "pipe/p_context.h"
#include "pipe/p_state.h"

struct fsa_context {
   struct pipe_context base;

   const struct pipe_blend_state *blend;
   const struct pipe_rasterizer_state *rasterizer;
   const struct pipe_depth_stencil_alpha_state *zsa;
   const struct pipe_vertex_element *velems;
   void *vs;
   void *fs;
};

struct pipe_context *fsa_create_context(struct pipe_screen *screen, void *priv,
                                        unsigned flags);

#endif