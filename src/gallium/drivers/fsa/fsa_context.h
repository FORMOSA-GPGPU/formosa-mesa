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

   struct pipe_framebuffer_state framebuffer;
   struct pipe_vertex_buffer vertex_buffers[PIPE_MAX_ATTRIBS];
   unsigned num_vertex_buffers;
   struct pipe_constant_buffer const_buffers[PIPE_SHADER_TYPES]
                                            [PIPE_MAX_CONSTANT_BUFFERS];
   struct pipe_viewport_state viewports[PIPE_MAX_VIEWPORTS];
   struct pipe_scissor_state scissors[PIPE_MAX_VIEWPORTS];
   void *sampler_states[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
   unsigned num_samplers[PIPE_SHADER_TYPES];
   struct pipe_sampler_view
      *sampler_views[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_SAMPLER_VIEWS];
   unsigned num_sampler_views[PIPE_SHADER_TYPES];

   void *vs;
   void *fs;
};

struct pipe_context *fsa_create_context(struct pipe_screen *screen, void *priv,
                                        unsigned flags);

static inline struct fsa_context *
fsa_context(struct pipe_context *pipe)
{
   return (struct fsa_context *)pipe;
}

#endif