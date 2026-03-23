#include "fsa_context.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"

static void
fsa_destroy_context(struct pipe_context *pctx)
{
   struct fsa_context *fctx = (struct fsa_context *)pctx;
   FREE(fctx);
}

static void
fsa_set_framebuffer_state(struct pipe_context *pctx,
                          const struct pipe_framebuffer_state *fb)
{
}

static void
fsa_set_blend_color(struct pipe_context *pctx,
                    const struct pipe_blend_color *state)
{
}

static void
fsa_set_sample_mask(struct pipe_context *pctx, unsigned sample_mask)
{
}

static void
fsa_set_clip_state(struct pipe_context *pctx,
                   const struct pipe_clip_state *state)
{
}

static void
fsa_set_polygon_stipple(struct pipe_context *pctx,
                        const struct pipe_poly_stipple *state)
{
}

static void
fsa_set_scissor_states(struct pipe_context *pctx, unsigned start_slot,
                       unsigned num_scissors,
                       const struct pipe_scissor_state *states)
{
}

static void
fsa_set_viewport_states(struct pipe_context *pctx, unsigned start_slot,
                        unsigned num_viewports,
                        const struct pipe_viewport_state *states)
{
}

static void
fsa_set_vertex_buffers(struct pipe_context *pctx, unsigned count,
                       const struct pipe_vertex_buffer *buffers)
{
}

static void
fsa_set_constant_buffer(struct pipe_context *pctx, enum pipe_shader_type shader,
                        uint index, bool take_ownership,
                        const struct pipe_constant_buffer *cb)
{
}

static void
fsa_bind_sampler_states(struct pipe_context *pctx, enum pipe_shader_type shader,
                        unsigned start_slot, unsigned num_samplers,
                        void **samplers)
{
}

static void
fsa_set_sampler_views(struct pipe_context *pctx, enum pipe_shader_type shader,
                      unsigned start_slot, unsigned num_views,
                      unsigned unbind_num_trailing_slots, bool take_ownership,
                      struct pipe_sampler_view **views)
{
}

static void
fsa_set_shader_buffers(struct pipe_context *pctx, enum pipe_shader_type shader,
                       unsigned start_slot, unsigned count,
                       const struct pipe_shader_buffer *buffers,
                       unsigned writable_bitmask)
{
}

static void
fsa_set_shader_images(struct pipe_context *pctx, enum pipe_shader_type shader,
                      unsigned start_slot, unsigned count,
                      unsigned unbind_num_trailing_slots,
                      const struct pipe_image_view *images)
{
}

static void
fsa_set_stencil_ref(struct pipe_context *pctx,
                    const struct pipe_stencil_ref ref)
{
}

static struct pipe_surface *
fsa_create_surface(struct pipe_context *pctx, struct pipe_resource *resource,
                   const struct pipe_surface *surf_tmpl)
{
   struct pipe_surface *surf = CALLOC_STRUCT(pipe_surface);
   if (!surf)
      return NULL;

   pipe_reference_init(&surf->reference, 1);
   pipe_resource_reference(&surf->texture, resource);
   surf->context = pctx;
   surf->format = surf_tmpl->format;
   surf->width = resource->width0;
   surf->height = resource->height0;

   return surf;
}

static void
fsa_surface_destroy(struct pipe_context *pctx, struct pipe_surface *surf)
{
   pipe_resource_reference(&surf->texture, NULL);
   FREE(surf);
}

static void
fsa_draw_vbo(struct pipe_context *pctx, const struct pipe_draw_info *info,
             unsigned drawid_offset,
             const struct pipe_draw_indirect_info *indirect,
             const struct pipe_draw_start_count_bias *draws, unsigned num_draws)
{
   /* No-op for now */
}

static void
fsa_clear(struct pipe_context *pctx, unsigned buffers,
          const struct pipe_scissor_state *scissor_state,
          const union pipe_color_union *color, double depth, unsigned stencil)
{
   /* No-op */
}

static void
fsa_flush(struct pipe_context *pctx, struct pipe_fence_handle **fence,
          unsigned flags)
{
   if (fence) {
      *fence = NULL;
   }
}

static void *
fsa_create_blend_state(struct pipe_context *pctx,
                       const struct pipe_blend_state *state)
{
   return mem_dup(state, sizeof(*state));
}

static void
fsa_bind_blend_state(struct pipe_context *pctx, void *hwcso)
{
   struct fsa_context *fctx = (struct fsa_context *)pctx;
   fctx->blend = hwcso;
}

static void
fsa_delete_blend_state(struct pipe_context *pctx, void *hwcso)
{
   FREE(hwcso);
}

static void *
fsa_create_rasterizer_state(struct pipe_context *pctx,
                            const struct pipe_rasterizer_state *state)
{
   return mem_dup(state, sizeof(*state));
}

static void
fsa_bind_rasterizer_state(struct pipe_context *pctx, void *hwcso)
{
   struct fsa_context *fctx = (struct fsa_context *)pctx;
   fctx->rasterizer = hwcso;
}

static void
fsa_delete_rasterizer_state(struct pipe_context *pctx, void *hwcso)
{
   FREE(hwcso);
}

static void *
fsa_create_depth_stencil_alpha_state(
   struct pipe_context *pctx,
   const struct pipe_depth_stencil_alpha_state *state)
{
   return mem_dup(state, sizeof(*state));
}

static void
fsa_bind_depth_stencil_alpha_state(struct pipe_context *pctx, void *hwcso)
{
   struct fsa_context *fctx = (struct fsa_context *)pctx;
   fctx->zsa = hwcso;
}

static void
fsa_delete_depth_stencil_alpha_state(struct pipe_context *pctx, void *hwcso)
{
   FREE(hwcso);
}

static void *
fsa_create_vertex_elements_state(struct pipe_context *pctx,
                                 unsigned num_elements,
                                 const struct pipe_vertex_element *elements)
{
   return mem_dup(elements, num_elements * sizeof(*elements));
}

static void
fsa_bind_vertex_elements_state(struct pipe_context *pctx, void *hwcso)
{
   struct fsa_context *fctx = (struct fsa_context *)pctx;
   fctx->velems = hwcso;
}

static void
fsa_delete_vertex_elements_state(struct pipe_context *pctx, void *hwcso)
{
   FREE(hwcso);
}

static void *
fsa_create_vs_state(struct pipe_context *pctx,
                    const struct pipe_shader_state *state)
{
   return mem_dup(state, sizeof(*state));
}

static void
fsa_bind_vs_state(struct pipe_context *pctx, void *hwcso)
{
   struct fsa_context *fctx = (struct fsa_context *)pctx;
   fctx->vs = hwcso;
}

static void
fsa_delete_vs_state(struct pipe_context *pctx, void *hwcso)
{
   FREE(hwcso);
}

static void *
fsa_create_fs_state(struct pipe_context *pctx,
                    const struct pipe_shader_state *state)
{
   return mem_dup(state, sizeof(*state));
}

static void
fsa_bind_fs_state(struct pipe_context *pctx, void *hwcso)
{
   struct fsa_context *fctx = (struct fsa_context *)pctx;
   fctx->fs = hwcso;
}

static void
fsa_delete_fs_state(struct pipe_context *pctx, void *hwcso)
{
   FREE(hwcso);
}

struct pipe_context *
fsa_create_context(struct pipe_screen *screen, void *priv, unsigned flags)
{
   struct fsa_context *fctx = CALLOC_STRUCT(fsa_context);
   if (!fctx)
      return NULL;

   fctx->base.screen = screen;
   fctx->base.priv = priv;
   fctx->base.destroy = fsa_destroy_context;

   /* Drawing and Flushing */
   fctx->base.flush = fsa_flush;
   fctx->base.clear = fsa_clear;
   fctx->base.draw_vbo = fsa_draw_vbo;

   /* Surfaces */
   fctx->base.create_surface = fsa_create_surface;
   fctx->base.surface_destroy = fsa_surface_destroy;

   /* State Setters */
   fctx->base.set_framebuffer_state = fsa_set_framebuffer_state;
   fctx->base.set_blend_color = fsa_set_blend_color;
   fctx->base.set_sample_mask = fsa_set_sample_mask;
   fctx->base.set_clip_state = fsa_set_clip_state;
   fctx->base.set_polygon_stipple = fsa_set_polygon_stipple;
   fctx->base.set_scissor_states = fsa_set_scissor_states;
   fctx->base.set_viewport_states = fsa_set_viewport_states;
   fctx->base.set_vertex_buffers = fsa_set_vertex_buffers;
   fctx->base.set_constant_buffer = fsa_set_constant_buffer;
   fctx->base.bind_sampler_states = fsa_bind_sampler_states;
   fctx->base.set_sampler_views = fsa_set_sampler_views;
   fctx->base.set_shader_buffers = fsa_set_shader_buffers;
   fctx->base.set_shader_images = fsa_set_shader_images;
   fctx->base.set_stencil_ref = fsa_set_stencil_ref;

   /* Blend State */
   fctx->base.create_blend_state = fsa_create_blend_state;
   fctx->base.bind_blend_state = fsa_bind_blend_state;
   fctx->base.delete_blend_state = fsa_delete_blend_state;

   /* Rasterizer State */
   fctx->base.create_rasterizer_state = fsa_create_rasterizer_state;
   fctx->base.bind_rasterizer_state = fsa_bind_rasterizer_state;
   fctx->base.delete_rasterizer_state = fsa_delete_rasterizer_state;

   /* Depth Stencil Alpha State */
   fctx->base.create_depth_stencil_alpha_state =
      fsa_create_depth_stencil_alpha_state;
   fctx->base.bind_depth_stencil_alpha_state =
      fsa_bind_depth_stencil_alpha_state;
   fctx->base.delete_depth_stencil_alpha_state =
      fsa_delete_depth_stencil_alpha_state;

   /* Vertex Elements */
   fctx->base.create_vertex_elements_state = fsa_create_vertex_elements_state;
   fctx->base.bind_vertex_elements_state = fsa_bind_vertex_elements_state;
   fctx->base.delete_vertex_elements_state = fsa_delete_vertex_elements_state;

   /* Shaders (Vertex & Fragment are the absolute bare minimum) */
   fctx->base.create_vs_state = fsa_create_vs_state;
   fctx->base.bind_vs_state = fsa_bind_vs_state;
   fctx->base.delete_vs_state = fsa_delete_vs_state;

   fctx->base.create_fs_state = fsa_create_fs_state;
   fctx->base.bind_fs_state = fsa_bind_fs_state;
   fctx->base.delete_fs_state = fsa_delete_fs_state;

   return &fctx->base;
}