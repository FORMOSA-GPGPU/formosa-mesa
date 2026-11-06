#include "fsa_draw.h"
#include <assert.h>
#include "uscsim/cbind.h"
#include "util/u_inlines.h"
#include "fsa_context.h"
#include "fsa_debug.h"
#include "fsa_emu_resource.h"
#include "fsa_screen.h"
#include "fsa_shader.h"

/* clang-foramt off */
/*
+-------------------+-----------------------+---------------------------------+
| Buffer            | Size                  | Description                     |
+-------------------+-----------------------+---------------------------------+
| gfxstate_addr     | sizeof(GfxState)      | Draw call state for GPU         |
| vs_info_addr      | sizeof(GfxVSInfo)     | VS Info                         |
| fs_info_addr      | sizeof(GfxFSInfo)     | FS Info                         |
| vs_out_addr       | n * vert_count * 16 B | varyings, each vec4[vert_count] |
| viss_addr         | prim_count * 40 B     | 5 x 64-bit words per primitive  |
| internal_fb_addr  | 32*32*8 B             | Per-bin depth+color scratch     |
+-------------------+-----------------------+---------------------------------+
*/
/* clang-format on */
static struct pipe_resource *
alloc_scratch(struct pipe_screen *screen, uint64_t size)
{
   struct pipe_resource templ = {0};
   templ.target = PIPE_BUFFER;
   templ.format = PIPE_FORMAT_R8_UNORM;
   templ.bind = PIPE_BIND_CUSTOM;
   templ.width0 = size;
   return fsa_emu_resource_create(screen, &templ);
}

void
fsa_draw_vbo(struct pipe_context *pctx, const struct pipe_draw_info *info,
             unsigned drawid_offset,
             const struct pipe_draw_indirect_info *indirect,
             const struct pipe_draw_start_count_bias *draws, unsigned num_draws)
{
   FSA_DBG(FSA_DEBUG_DRAW, "fsa_draw_vbo\n");
   assert(num_draws == 1 && "Only supports 1 draw");
   struct fsa_context *fsa = fsa_context(pctx);
   struct fsa_screen *screen = fsa_screen(fsa->base.screen);
   GfxEmu *emu = screen->emu;
   struct fsa_shader_state *vs = fsa->vs;
   struct fsa_shader_state *fs = fsa->fs;

   const struct fsa_vertex_element_state *ve = fsa->velem_state;
   uint64_t vert_count = draws[0].count;

   GfxState gfxstate = {0};
   /* Setup Vertex attrib GfxLayout */
   for (unsigned i = 0; i < ve->count; i++) {
      const struct pipe_vertex_element *e = &ve->elements[i];
      const struct pipe_vertex_buffer *vb =
         &fsa->vertex_buffers[e->vertex_buffer_index];
      uint64_t buf_base = fsa_emu_resource(vb->buffer.resource)->dmem_offset;
      /* `draws[0].start is the `first` in glDrawArrays(mode, first, count), my
       * graphics pipeline only supports drawing from the first */
      assert(draws[0].start == 0);
      gfxstate.vert_attrib_generic[i].base =
         buf_base + vb->buffer_offset + e->src_offset +
         (uint64_t)draws[0].start * e->src_stride;
      gfxstate.vert_attrib_generic[i].stride = e->src_stride;
   }

   /* Buffer reallocation for varyings and viss */
   /* pos + psize + n_var, each vec4, num_vert */
   int num_slots = __builtin_popcountll(fsa->vs->active_varying_slot);
   uint64_t required_scratch_size = (2 + num_slots) * vert_count * 16;
   if (fsa->varying_scratch_buffer_size < required_scratch_size) {
      if (fsa->varying_scratch_buffer)
         pipe_resource_reference(&fsa->varying_scratch_buffer, NULL);
      fsa->varying_scratch_buffer =
         alloc_scratch(fsa->base.screen, required_scratch_size);
      fsa->varying_scratch_buffer_size = required_scratch_size;
   }

   /* Only supports non-indexed GL_TRIANGLE now, so prim_count == vert_count/3 */
   uint64_t required_viss_size = (vert_count / 3) * 40;
   if (fsa->viss_buffer_size < required_viss_size) {
      if (fsa->viss_buffer)
         pipe_resource_reference(&fsa->viss_buffer, NULL);
      fsa->viss_buffer = alloc_scratch(fsa->base.screen, required_viss_size);
      fsa->viss_buffer_size = required_viss_size;
   }
   gfxstate.VisS_base = fsa_emu_resource(fsa->viss_buffer)->dmem_offset;

   /* Setup varyings GfxLayout */
   uint64_t vs_out_addr =
      fsa_emu_resource(fsa->varying_scratch_buffer)->dmem_offset;
   uint64_t vs_out_stride = 16 * vert_count;
   gfxstate.varying_slot_pos[0] = (GfxLayout){vs_out_addr, 16};
   gfxstate.varying_slot_psize[0] =
      (GfxLayout){vs_out_addr + vs_out_stride, 16};
   for (int i = 0; i < num_slots; i++) {
      gfxstate.varying_slot_var[i] =
         (GfxLayout){vs_out_addr + vs_out_stride * (i + 2),
                     fsa->vs->varying_slot_stride[i]};
   }

   /* Viewport, Gallium will send Y-flipped viewport scaling and offset, we must
    * unflip the values */
   gfxstate.viewport_scale[0] = fsa->viewports[0].scale[0];
   gfxstate.viewport_scale[1] = -fsa->viewports[0].scale[1];
   gfxstate.viewport_scale[2] = fsa->viewports[0].scale[2];
   gfxstate.viewport_offset[0] = fsa->viewports[0].translate[0];
   gfxstate.viewport_offset[1] =
      fsa->framebuffer.height - fsa->viewports[0].translate[1];
   gfxstate.viewport_offset[2] = fsa->viewports[0].translate[2];
   FSA_DBG(FSA_DEBUG_DRAW, "gfxstate viewport scale: %f %f %f\n",
           gfxstate.viewport_scale[0], gfxstate.viewport_scale[1],
           gfxstate.viewport_scale[2]);
   FSA_DBG(FSA_DEBUG_DRAW, "gfxstate viewport offset: %f %f %f\n",
           gfxstate.viewport_offset[0], gfxstate.viewport_offset[1],
           gfxstate.viewport_offset[2]);

   /* Allocate persistent state buffers on first use */
   if (!fsa->gfxstate_buffer) {
      fsa->gfxstate_buffer = alloc_scratch(fsa->base.screen, sizeof(GfxState));
   }
   if (!fsa->vsinfo_buffer) {
      fsa->vsinfo_buffer = alloc_scratch(fsa->base.screen, sizeof(GfxVSInfo));
   }
   if (!fsa->fsinfo_buffer) {
      fsa->fsinfo_buffer = alloc_scratch(fsa->base.screen, sizeof(GfxFSInfo));
   }
   if (!fsa->internal_fb_buffer) {
      fsa->internal_fb_buffer =
         alloc_scratch(fsa->base.screen, 32 * 32 * sizeof(uint64_t));
   }

   uint64_t gfxstate_addr = fsa_emu_resource(fsa->gfxstate_buffer)->dmem_offset;
   uint64_t vs_info_addr = fsa_emu_resource(fsa->vsinfo_buffer)->dmem_offset;
   uint64_t fs_info_addr = fsa_emu_resource(fsa->fsinfo_buffer)->dmem_offset;
   uint64_t internal_fb_addr =
      fsa_emu_resource(fsa->internal_fb_buffer)->dmem_offset;

   assert(fsa->framebuffer.cbufs[0] && "Always have color buffer");
   uint64_t color_base =
      fsa_emu_resource(fsa->framebuffer.cbufs[0]->texture)->dmem_offset;
   assert(fsa->framebuffer.zsbuf && "Always have zsbuffer");
   uint64_t zs_base =
      fsa_emu_resource(fsa->framebuffer.zsbuf->texture)->dmem_offset;

   FSA_DBG(FSA_DEBUG_DRAW, "draw_vbo:gfx_state_addr:0x%lx\n", gfxstate_addr);
   FSA_DBG(FSA_DEBUG_DRAW, "draw_vbo:vs_info_addr:0x%lx\n", vs_info_addr);
   FSA_DBG(FSA_DEBUG_DRAW, "draw_vbo:fs_info_addr:0x%lx\n", fs_info_addr);
   gfxemu_setup(emu, vs->kernel, fs->kernel, &gfxstate, gfxstate_addr,
                vs_info_addr, fs_info_addr, color_base, zs_base,
                internal_fb_addr);
   FSA_DBG(FSA_DEBUG_DRAW, "draw_vbo:draw_arrays\n");
   gfxemu_draw_arrays(emu, 1, vert_count);
}