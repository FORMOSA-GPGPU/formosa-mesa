#include "fsa_context.h"
#include "util/format/u_format.h"
#include "util/u_framebuffer.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_transfer.h"
#include "fsa_debug.h"
#include "fsa_draw.h"
#include "fsa_emu_resource.h"
#include "fsa_sampler_state.h"
#include "fsa_sampler_view.h"
#include "fsa_shader.h"
#include "fsa_transfer.h"

static void
fsa_destroy_context(struct pipe_context *pctx)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_destroy_context\n");
   struct fsa_context *fctx = (struct fsa_context *)pctx;
   FREE(fctx);
}

static void
fsa_set_framebuffer_state(struct pipe_context *pctx,
                          const struct pipe_framebuffer_state *fb)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_set_framebuffer_state\n");
   struct fsa_context *ctx = (struct fsa_context *)pctx;
   util_copy_framebuffer_state(&ctx->framebuffer, fb);
}

static void
fsa_set_blend_color(struct pipe_context *pctx,
                    const struct pipe_blend_color *state)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_set_blend_color\n");
}

static void
fsa_set_sample_mask(struct pipe_context *pctx, unsigned sample_mask)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_set_sample_mask\n");
}

static void
fsa_set_clip_state(struct pipe_context *pctx,
                   const struct pipe_clip_state *state)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_set_clip_state\n");
}

static void
fsa_set_polygon_stipple(struct pipe_context *pctx,
                        const struct pipe_poly_stipple *state)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_set_polygon_stipple\n");
}

static void
fsa_set_scissor_states(struct pipe_context *pctx, unsigned start_slot,
                       unsigned num_scissors,
                       const struct pipe_scissor_state *states)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_set_scissor_states\n");
   struct fsa_context *fsa = fsa_context(pctx);
   memcpy(&fsa->scissors[start_slot], states,
          sizeof(struct pipe_scissor_state) * num_scissors);
}

static void
fsa_set_viewport_states(struct pipe_context *pctx, unsigned start_slot,
                        unsigned num_viewports,
                        const struct pipe_viewport_state *states)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_set_viewport_states\n");
   struct fsa_context *fsa = fsa_context(pctx);
   memcpy(&fsa->viewports[start_slot], states,
          sizeof(struct pipe_viewport_state) * num_viewports);
}

static void
fsa_set_vertex_buffers(struct pipe_context *pctx, unsigned count,
                       const struct pipe_vertex_buffer *buffers)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_set_vertex_buffers\n");
   struct fsa_context *fsa = fsa_context(pctx);
   unsigned i;
   for (i = 0; i < count; i++) {
      struct pipe_vertex_buffer *dst = &fsa->vertex_buffers[i];
      const struct pipe_vertex_buffer *src = &buffers[i];

      dst->is_user_buffer = src->is_user_buffer;
      dst->buffer_offset = src->buffer_offset;
      pipe_resource_reference(&dst->buffer.resource, NULL);

      if (src->is_user_buffer) {
         dst->buffer.user = src->buffer.user;
      } else {
         dst->buffer.resource = src->buffer.resource;
      }
   }
   /* Clean up unused buffers */
   for (i = count; i < fsa->num_vertex_buffers; i++) {
      struct pipe_vertex_buffer *dst = &fsa->vertex_buffers[i];
      pipe_resource_reference(&dst->buffer.resource, NULL);
      dst->is_user_buffer = false;
      dst->buffer.user = NULL;
   }
   fsa->num_vertex_buffers = count;
}

static void
fsa_set_constant_buffer(struct pipe_context *pctx, enum pipe_shader_type shader,
                        uint index, bool take_ownership,
                        const struct pipe_constant_buffer *cb)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_set_constant_buffer\n");
   struct fsa_context *fsa = fsa_context(pctx);
   struct pipe_constant_buffer *dst = &fsa->const_buffers[shader][index];
   if (cb) {
      dst->buffer_size = cb->buffer_size;
      dst->buffer_offset = cb->buffer_offset;
      dst->user_buffer = cb->user_buffer;

      if (take_ownership) {
         pipe_resource_reference(&dst->buffer, NULL);
         dst->buffer = cb->buffer;
      } else {
         pipe_resource_reference(&dst->buffer, cb->buffer);
      }
   } else {
      pipe_resource_reference(&dst->buffer, NULL);
      memset(dst, 0, sizeof(*dst));
   }
}

static void *
fsa_create_sampler_state(struct pipe_context *pctx,
                         const struct pipe_sampler_state *state)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_create_sampler_state\n");
   struct fsa_sampler_state *sampler = CALLOC_STRUCT(fsa_sampler_state);
   if (!sampler)
      return NULL;
   sampler->base = *state;
   return (void *)sampler;
}

static void
fsa_bind_sampler_states(struct pipe_context *pctx, enum pipe_shader_type shader,
                        unsigned start_slot, unsigned num_samplers,
                        void **samplers)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_bind_sampler_states\n");
   struct fsa_context *ctx = (struct fsa_context *)pctx;
   assert(shader == PIPE_SHADER_VERTEX || shader == PIPE_SHADER_FRAGMENT);
   for (unsigned i = start_slot; i < num_samplers; i++) {
      ctx->sampler_states[shader][i] = samplers[i];
   }
}

static void
fsa_delete_sampler_state(struct pipe_context *pctx, void *sampler)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_delete_sampler_state\n");
   FREE(sampler);
}

static struct pipe_sampler_view *
fsa_create_sampler_view(struct pipe_context *pctx,
                        struct pipe_resource *texture,
                        const struct pipe_sampler_view *templat)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_create_sampler_view\n");
   struct fsa_sampler_view *view = CALLOC_STRUCT(fsa_sampler_view);
   if (!view)
      return NULL;
   view->base = *templat;
   /* Init ref counter */
   pipe_reference_init(&view->base.reference, 1);
   view->base.context = pctx;
   view->base.texture = NULL;
   pipe_resource_reference(&view->base.texture, texture);
   return &view->base;
}

static void
fsa_sampler_view_destroy(struct pipe_context *pctx,
                         struct pipe_sampler_view *view)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_sampler_view_destroy\n");
   pipe_resource_reference(&view->texture, NULL);
   FREE(view);
}

static void
fsa_set_sampler_views(struct pipe_context *pctx, enum pipe_shader_type shader,
                      unsigned start_slot, unsigned num_views,
                      unsigned unbind_num_trailing_slots, bool take_ownership,
                      struct pipe_sampler_view **views)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_set_sampler_views\n");
   struct fsa_context *ctx = fsa_context(pctx);
   assert(shader == PIPE_SHADER_VERTEX || shader == PIPE_SHADER_FRAGMENT);
   for (unsigned i = start_slot; i < num_views; i++) {
      struct pipe_sampler_view **dst = &ctx->sampler_views[shader][i];
      struct pipe_sampler_view *src = views ? views[i] : NULL;

      if (take_ownership) {
         pipe_sampler_view_reference(&views[i], NULL);
         *dst = src;
      } else {
         pipe_sampler_view_reference(dst, src);
      }
   }
   /* Clean up trailing slots */
   for (unsigned i = start_slot + num_views; i < unbind_num_trailing_slots;
        i++) {
      struct pipe_sampler_view **dst = &ctx->sampler_views[shader][i];
      pipe_sampler_view_reference(dst, NULL);
   }
}

static void
fsa_set_shader_buffers(struct pipe_context *pctx, enum pipe_shader_type shader,
                       unsigned start_slot, unsigned count,
                       const struct pipe_shader_buffer *buffers,
                       unsigned writable_bitmask)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_set_shader_buffers\n");
}

static void
fsa_set_shader_images(struct pipe_context *pctx, enum pipe_shader_type shader,
                      unsigned start_slot, unsigned count,
                      unsigned unbind_num_trailing_slots,
                      const struct pipe_image_view *images)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_set_shader_images\n");
}

static void
fsa_set_stencil_ref(struct pipe_context *pctx,
                    const struct pipe_stencil_ref ref)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_set_stencil_ref\n");
}

static struct pipe_surface *
fsa_create_surface(struct pipe_context *pctx, struct pipe_resource *resource,
                   const struct pipe_surface *surf_tmpl)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_create_surface\n");
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
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_surface_destroy\n");
   pipe_resource_reference(&surf->texture, NULL);
   FREE(surf);
}

static void
fsa_clear(struct pipe_context *pctx, unsigned buffers,
          const struct pipe_scissor_state *scissor_state,
          const union pipe_color_union *color, double depth, unsigned stencil)
{
   FSA_DBG(FSA_DEBUG_DRAW, "fsa_clear\n");
}

static void
fsa_flush(struct pipe_context *pctx, struct pipe_fence_handle **fence,
          unsigned flags)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_flush\n");
   if (fence) {
      *fence = NULL;
   }
}

static void *
fsa_create_blend_state(struct pipe_context *pctx,
                       const struct pipe_blend_state *state)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_create_blend_state\n");
   return mem_dup(state, sizeof(*state));
}

static void
fsa_bind_blend_state(struct pipe_context *pctx, void *hwcso)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_bind_blend_state\n");
   struct fsa_context *fctx = (struct fsa_context *)pctx;
   fctx->blend = hwcso;
}

static void
fsa_delete_blend_state(struct pipe_context *pctx, void *hwcso)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_delete_blend_state\n");
   FREE(hwcso);
}

static void *
fsa_create_rasterizer_state(struct pipe_context *pctx,
                            const struct pipe_rasterizer_state *state)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_create_rasterizer_state\n");
   return mem_dup(state, sizeof(*state));
}

static void
fsa_bind_rasterizer_state(struct pipe_context *pctx, void *hwcso)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_bind_rasterizer_state\n");
   struct fsa_context *fctx = (struct fsa_context *)pctx;
   fctx->rasterizer = hwcso;
}

static void
fsa_delete_rasterizer_state(struct pipe_context *pctx, void *hwcso)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_delete_rasterizer_state\n");
   FREE(hwcso);
}

static void *
fsa_create_depth_stencil_alpha_state(
   struct pipe_context *pctx,
   const struct pipe_depth_stencil_alpha_state *state)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_create_depth_stencil_alpha_state\n");
   return mem_dup(state, sizeof(*state));
}

static void
fsa_bind_depth_stencil_alpha_state(struct pipe_context *pctx, void *hwcso)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_bind_depth_stencil_alpha_state\n");
   struct fsa_context *fctx = (struct fsa_context *)pctx;
   fctx->zsa = hwcso;
}

static void
fsa_delete_depth_stencil_alpha_state(struct pipe_context *pctx, void *hwcso)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_delete_depth_stencil_alpha_state\n");
   FREE(hwcso);
}

static void *
fsa_create_vertex_elements_state(struct pipe_context *pctx,
                                 unsigned num_elements,
                                 const struct pipe_vertex_element *elements)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_create_vertex_elements_state\n");
   return mem_dup(elements, num_elements * sizeof(*elements));
}

static void
fsa_bind_vertex_elements_state(struct pipe_context *pctx, void *hwcso)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_bind_vertex_elements_state\n");
   struct fsa_context *fctx = (struct fsa_context *)pctx;
   fctx->velems = hwcso;
}

static void
fsa_delete_vertex_elements_state(struct pipe_context *pctx, void *hwcso)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_delete_vertex_elements_state\n");
   FREE(hwcso);
}

static void
fsa_bind_vs_state(struct pipe_context *pctx, void *hwcso)
{
   FSA_DBG(FSA_DEBUG_SHADER, "fsa_bind_vs_state\n");
   struct fsa_context *fctx = (struct fsa_context *)pctx;
   fctx->vs = hwcso;
}

static void
fsa_delete_vs_state(struct pipe_context *pctx, void *hwcso)
{
   FSA_DBG(FSA_DEBUG_SHADER, "fsa_delete_vs_state\n");
   struct fsa_shader_state *state = fsa_shader_state(hwcso);
   free(state->compiled_bin);
   FREE(hwcso);
}

static void
fsa_bind_fs_state(struct pipe_context *pctx, void *hwcso)
{
   FSA_DBG(FSA_DEBUG_SHADER, "fsa_bind_fs_state\n");
   struct fsa_context *fctx = (struct fsa_context *)pctx;
   fctx->fs = hwcso;
}

static void
fsa_delete_fs_state(struct pipe_context *pctx, void *hwcso)
{
   FSA_DBG(FSA_DEBUG_SHADER, "fsa_delete_fs_state\n");
   struct fsa_shader_state *state = fsa_shader_state(hwcso);
   free(state->compiled_bin);
   FREE(hwcso);
}

static void *
fsa_buffer_map(struct pipe_context *ctx, struct pipe_resource *resource,
               unsigned level, unsigned usage, const struct pipe_box *box,
               struct pipe_transfer **out_transfer)
{
   FSA_DBG(FSA_DEBUG_RESOURCE, "fsa_buffer_map\n");
   struct fsa_emu_resource *rsc = fsa_emu_resource(resource);
   struct fsa_transfer *trans = CALLOC_STRUCT(fsa_transfer);
   if (!trans)
      return NULL;

   trans->base.resource = resource;
   trans->base.level = level;
   trans->base.usage = usage;
   trans->base.box = *box;
   trans->base.stride = 0;
   trans->base.layer_stride = 0;

   trans->staging = MALLOC(box->width);
   if (!trans->staging) {
      FREE(trans);
   }

   if (usage & PIPE_MAP_READ) {
      uint64_t addr = rsc->dmem_offset + box->x;
      /* TODO: Read from gfxemu ifmem */
   }

   *out_transfer = &trans->base;
   return trans->staging;
}

static void
fsa_buffer_unmap(struct pipe_context *ctx, struct pipe_transfer *transfer)
{
   FSA_DBG(FSA_DEBUG_RESOURCE, "fsa_buffer_unmap\n");
   struct fsa_transfer *trans = fsa_transfer(transfer);
   struct fsa_emu_resource *rsc = fsa_emu_resource(trans->base.resource);
   if (trans->base.usage & PIPE_MAP_WRITE) {
      uint64_t addr = rsc->dmem_offset + trans->base.box.x;
      /* TODO: Write to gfxemu ifmem */
   }
   FREE(trans->staging);
   FREE(trans);
}

static void
fsa_buffer_subdata(struct pipe_context *ctx, struct pipe_resource *rsc,
                   unsigned usage, unsigned offset, unsigned size,
                   const void *data)
{
   FSA_DBG(FSA_DEBUG_RESOURCE, "fsa_buffer_subdata\n");
   u_default_buffer_subdata(ctx, rsc, usage, offset, size, data);
}

static void *
fsa_texture_map(struct pipe_context *ctx, struct pipe_resource *prsc,
                unsigned level, unsigned usage, const struct pipe_box *box,
                struct pipe_transfer **out_transfer)
{
   FSA_DBG(FSA_DEBUG_RESOURCE, "fsa_texture_map\n");
   struct fsa_emu_resource *rsc = fsa_emu_resource(prsc);
   struct fsa_transfer *trans = CALLOC_STRUCT(fsa_transfer);
   if (!trans)
      return NULL;

   trans->base.resource = prsc;
   trans->base.level = level;
   trans->base.usage = usage;
   trans->base.box = *box;

   unsigned bytes_per_pixel = util_format_get_blocksize(prsc->format);
   trans->base.stride = box->width * bytes_per_pixel;
   trans->base.layer_stride = trans->base.stride * box->height;

   size_t size = trans->base.layer_stride * box->depth;
   trans->staging = MALLOC(size);
   if (!trans->staging) {
      FREE(trans);
   }

   /* Copy to staging buffer */
   if (usage & PIPE_MAP_READ) {
      uint64_t vram_stride = rsc->base.width0 * bytes_per_pixel;

      uint8_t *dst = (uint8_t *)trans->staging;

      for (uint64_t z = box->z; z < box->depth; z++) {
         for (uint64_t y = box->y; y < box->height; y++) {

            uint64_t vram_row_offset =
               (z * (vram_stride * rsc->base.height0)) + // Z offset
               (y * vram_stride) +                       // Y offset
               (box->x * bytes_per_pixel);               // X offset

            uint64_t addr = rsc->dmem_offset + vram_row_offset;
            /* TODO: Copy one row from vram, size=trans->base.stride */
            dst += trans->base.stride;
         }
      }
   }
   *out_transfer = &trans->base;
   return trans->staging;
}

static void
fsa_texture_unmap(struct pipe_context *ctx, struct pipe_transfer *transfer)
{
   FSA_DBG(FSA_DEBUG_RESOURCE, "fsa_texture_unmap\n");
   struct fsa_transfer *trans = fsa_transfer(transfer);
   struct fsa_emu_resource *rsc = fsa_emu_resource(trans->base.resource);

   if (transfer->usage & PIPE_MAP_WRITE) {
      unsigned bpp = util_format_get_blocksize(transfer->resource->format);
      uint64_t vram_stride = transfer->resource->width0 * bpp;
      uint8_t *src = (uint8_t *)trans->staging;

      for (uint64_t z = transfer->box.z; z < transfer->box.depth; z++) {
         for (uint64_t y = transfer->box.y; y < transfer->box.height; y++) {

            uint64_t vram_row_offset =
               (z * (vram_stride * transfer->resource->height0)) + // Z offset
               (y * vram_stride) +                                 // Y offset
               (transfer->box.x * bpp);                            // X offset

            uint64_t addr = rsc->dmem_offset + vram_row_offset;
            /* TODO: Copy to vram , size = trans->base.stride */
            src += trans->base.stride;
         }
      }
   }

   FREE(trans->staging);
   FREE(trans);
}

static void
fsa_texture_subdata(struct pipe_context *ctx, struct pipe_resource *rsc,
                    unsigned level, unsigned usage, const struct pipe_box *box,
                    const void *data, unsigned stride, uintptr_t layer_stride)
{
   FSA_DBG(FSA_DEBUG_RESOURCE, "fsa_texture_subdata\n");
   u_default_texture_subdata(ctx, rsc, level, usage, box, data, stride,
                             layer_stride);
}

struct pipe_context *
fsa_create_context(struct pipe_screen *screen, void *priv, unsigned flags)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_create_context\n");
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
   fctx->base.set_shader_buffers = fsa_set_shader_buffers;
   fctx->base.set_shader_images = fsa_set_shader_images;
   fctx->base.set_stencil_ref = fsa_set_stencil_ref;

   /* Sampler State */
   fctx->base.create_sampler_state = fsa_create_sampler_state;
   fctx->base.bind_sampler_states = fsa_bind_sampler_states;
   fctx->base.delete_sampler_state = fsa_delete_sampler_state;

   /* Sampler View */
   fctx->base.create_sampler_view = fsa_create_sampler_view;
   fctx->base.set_sampler_views = fsa_set_sampler_views;
   fctx->base.sampler_view_destroy = fsa_sampler_view_destroy;

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

   /* Mem transfer */
   fctx->base.buffer_map = fsa_buffer_map;
   fctx->base.buffer_unmap = fsa_buffer_unmap;
   fctx->base.buffer_subdata = fsa_buffer_subdata;
   fctx->base.texture_map = fsa_texture_map;
   fctx->base.texture_unmap = fsa_texture_unmap;
   fctx->base.texture_subdata = fsa_texture_subdata;

   return &fctx->base;
}