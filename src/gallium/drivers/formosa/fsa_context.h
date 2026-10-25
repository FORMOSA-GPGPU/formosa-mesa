#ifndef FSA_CONTEXT_H
#define FSA_CONTEXT_H

#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "fsa_shader.h"

struct fsa_vertex_element_state {
   struct pipe_vertex_element *elements;
   unsigned count;
};

struct fsa_context {
   struct pipe_context base;

   const struct pipe_blend_state *blend;
   const struct pipe_rasterizer_state *rasterizer;
   const struct pipe_depth_stencil_alpha_state *zsa;
   struct fsa_vertex_element_state *velem_state;
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
   struct fsa_shader_state *vs;
   struct fsa_shader_state *fs;

   struct pipe_resource *varying_scratch_buffer;
   uint64_t varying_scratch_buffer_size;
   struct pipe_resource *viss_buffer;
   uint64_t viss_buffer_size;
   struct pipe_resource *gfxstate_buffer;
   struct pipe_resource *vsinfo_buffer;
   struct pipe_resource *fsinfo_buffer;
   struct pipe_resource *internal_fb_buffer;
};

struct pipe_context *fsa_create_context(struct pipe_screen *screen, void *priv,
                                        unsigned flags);

static inline struct fsa_vertex_element_state *
fsa_vertex_element_state(void *state)
{
   return (struct fsa_vertex_element_state *)state;
}

static inline struct fsa_context *
fsa_context(struct pipe_context *pipe)
{
   return (struct fsa_context *)pipe;
}

#endif