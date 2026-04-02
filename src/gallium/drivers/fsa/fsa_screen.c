#include "fsa_screen.h"
#include <stdio.h>
#include "pipe/p_defines.h"
#include "util/format/u_format.h"
#include "util/u_memory.h"
#include "util/u_screen.h"
#include "falloc.h"
#include "fsa_compiler.h"
#include "fsa_context.h"
#include "fsa_debug.h"
#include "fsa_emu_resource.h"
#include "fsa_util.h"

int FSA_DEBUG = 0;

static const struct debug_named_value fsa_debug_names[] = {
   {"screen", FSA_DEBUG_SCREEN, "Screen operations"},
   {"context", FSA_DEBUG_CONTEXT, "Context operations"},
   {"draw", FSA_DEBUG_DRAW, "Draw operations"},
   {"resource", FSA_DEBUG_RESOURCE, "Resource operations"},
   {"shader", FSA_DEBUG_SHADER, "Shader operations"},
   {"nir", FSA_DEBUG_NIR, "NIR operations"},
   {"asm", FSA_DEBUG_ASM, "ASM operations"},
   {"verbose", FSA_DEBUG_ALL, "All (except emu)"},
   {"emu_vs", FSA_DEBUG_EMU_VS, "ggdb vs"},
   {"emu_fs", FSA_DEBUG_EMU_FS, "ggdb fs"},
   DEBUG_NAMED_VALUE_END};

void
fsa_debug_init(void)
{
   FSA_DEBUG = debug_get_flags_option("FSA_DEBUG", fsa_debug_names, 0);
}

static const char *
fsa_screen_get_name(struct pipe_screen *pscreen)
{
   FSA_DBG(FSA_DEBUG_SCREEN, "fsa_screen_get_name\n");
   return "GFXEmu";
}

static const char *
fsa_screen_get_vendor(struct pipe_screen *pscreen)
{
   FSA_DBG(FSA_DEBUG_SCREEN, "fsa_screen_get_vendor\n");
   return "Formosa";
}

static void
fsa_screen_destroy(struct pipe_screen *pscreen)
{
   FSA_DBG(FSA_DEBUG_SCREEN, "fsa_screen_destroy\n");
   struct fsa_screen *screen = fsa_screen(pscreen);
   gfxemu_destroy(screen->emu);
   FREE(screen);
}

static bool
fsa_screen_is_format_supported(struct pipe_screen *screen,
                               enum pipe_format format,
                               enum pipe_texture_target target,
                               unsigned sample_count,
                               unsigned storage_sample_count, unsigned bindings)
{
   if (sample_count > 1 || storage_sample_count > 1) {
      return false;
   }
   if (target != PIPE_BUFFER && target != PIPE_TEXTURE_2D &&
       target != PIPE_TEXTURE_RECT && target != PIPE_TEXTURE_2D_ARRAY) {
      return false;
   }

   /* Additional format support added to enable gles3.0 */
   switch (format) {
   case PIPE_FORMAT_B8G8R8A8_UNORM:
   case PIPE_FORMAT_B8G8R8A8_SRGB:
      if (bindings & PIPE_BIND_DEPTH_STENCIL) {
         return false;
      }
      return true;

   case PIPE_FORMAT_R8G8B8A8_UNORM:
   case PIPE_FORMAT_R8G8B8A8_SRGB:
   case PIPE_FORMAT_R8G8B8A8_SNORM:
   case PIPE_FORMAT_R8G8_UNORM:
   case PIPE_FORMAT_R8_UNORM:
   case PIPE_FORMAT_R16G16B16A16_FLOAT:
   case PIPE_FORMAT_R32G32B32A32_FLOAT:
   case PIPE_FORMAT_R11G11B10_FLOAT:
   case PIPE_FORMAT_R9G9B9E5_FLOAT:
      if (bindings & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DISPLAY_TARGET |
                      PIPE_BIND_DEPTH_STENCIL)) {
         return false;
      }
      return true;

   case PIPE_FORMAT_Z32_FLOAT:
   case PIPE_FORMAT_Z32_UNORM:
   case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
      if (bindings & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DISPLAY_TARGET)) {
         return false;
      }
      return true;

   default:
      return false;
   }
}

static const void *
fsa_get_compiler_options(struct pipe_screen *screen, enum pipe_shader_ir ir,
                         enum pipe_shader_type shader)
{
   FSA_DBG(FSA_DEBUG_SHADER, "fsa_get_compiler_options\n");
   if (ir != PIPE_SHADER_IR_NIR) {
      return NULL;
   }
   return &fsa_nir_option;
}

static void
fsa_init_screen_caps(struct fsa_screen *screen)
{
   FSA_DBG(FSA_DEBUG_SCREEN, "fsa_init_screen_caps\n");
   struct pipe_caps *caps = (struct pipe_caps *)&screen->base.caps;

   u_init_pipe_screen_caps(&screen->base, 1);

   caps->npot_textures = true;
   caps->blend_equation_separate = true;
   caps->draw_indirect = false;
   caps->multi_draw_indirect = false;
   /* For MaxViewportWidth/Height 640*480 */
   caps->max_texture_2d_size = 16384;

   /* Features added to support gles 3.0 */
   caps->glsl_feature_level = 130;
   caps->glsl_feature_level_compatibility = 130;
   caps->fragment_shader_texture_lod = true;
   caps->texture_float_linear = true;
   caps->texture_half_float_linear = true;
   caps->mixed_framebuffer_sizes = true;
   caps->vs_instanceid = true;
   caps->vertex_element_instance_divisor = true;
   caps->primitive_restart_fixed_index = true;
   caps->max_texture_array_layers = 256;
   caps->max_stream_output_buffers = 4;
   caps->max_render_targets = 4;
   caps->essl_feature_level = 130;
}

static void
fsa_init_shader_caps(struct fsa_screen *screen)
{
   FSA_DBG(FSA_DEBUG_SCREEN, "fsa_init_shader_caps\n");
   for (unsigned i = 0; i <= PIPE_SHADER_COMPUTE; i++) {
      struct pipe_shader_caps *caps =
         (struct pipe_shader_caps *)&screen->base.shader_caps[i];

      if (i != PIPE_SHADER_VERTEX && i != PIPE_SHADER_FRAGMENT)
         continue;

      caps->supported_irs = 1 << PIPE_SHADER_IR_NIR;
      caps->max_instructions = 16384;
      caps->max_alu_instructions = 16384;
      caps->max_tex_instructions = 16384;
      caps->max_tex_indirections = 16384;
      caps->max_control_flow_depth = 32;
      caps->max_inputs = 32;
      caps->max_outputs = 32;
      caps->max_const_buffer0_size = 4096 * sizeof(float[4]);
      caps->max_const_buffers = 16;
      caps->max_temps = 256;
      caps->max_texture_samplers = 8;
      caps->max_sampler_views = 8;
      caps->indirect_const_addr = true;
   }
}

static void
fsa_fence_reference(struct pipe_screen *screen, struct pipe_fence_handle **ptr,
                    struct pipe_fence_handle *fence)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_fence_reference\n");
   *ptr = fence;
}

static bool
fsa_fence_finish(struct pipe_screen *screen, struct pipe_context *ctx,
                 struct pipe_fence_handle *fence, uint64_t timeout)
{
   FSA_DBG(FSA_DEBUG_CONTEXT, "fsa_fence_finish\n");
   return true;
}

static void
fsa_flush_frontbuffer(struct pipe_screen *screen, struct pipe_context *ctx,
                      struct pipe_resource *resource, unsigned level,
                      unsigned layer, void *winsys_drawable_handle,
                      unsigned nboxes, struct pipe_box *subbox)
{
   FSA_DBG(FSA_DEBUG_SCREEN, "fsa_flush_frontbuffer\n");
   struct fsa_screen *fsa = fsa_screen(screen);
   struct fsa_emu_resource *rsc = fsa_emu_resource(resource);
   if (rsc->dt) {
      void *host_map =
         fsa->winsys->displaytarget_map(fsa->winsys, rsc->dt, PIPE_MAP_WRITE);
      if (host_map) {
         unsigned width = resource->width0;
         unsigned height = resource->height0;
         unsigned src_stride = rsc->stride;
         for (unsigned y = 0; y < height; y++) {
            uint8_t *dst_row = (uint8_t *)host_map + y * rsc->stride;
            uint64_t src_addr = rsc->dmem_offset + (uint64_t)y * src_stride;
            gfxemu_read_mem(fsa->emu, dst_row, src_addr, width * 4);
         }
         fsa->winsys->displaytarget_unmap(fsa->winsys, rsc->dt);
      }
      fsa->winsys->displaytarget_display(
         fsa->winsys, rsc->dt, winsys_drawable_handle, nboxes, subbox);
   }
}

static bool
fsa_resource_get_handle(struct pipe_screen *screen,
                        struct pipe_context *context, struct pipe_resource *tex,
                        struct winsys_handle *handle, unsigned usage)
{
   FSA_DBG(FSA_DEBUG_RESOURCE, "fsa_resource_get_handle\n");
   struct fsa_screen *fsa = fsa_screen(screen);
   struct fsa_emu_resource *rsc = fsa_emu_resource(tex);
   if (!rsc->dt)
      return false;
   return fsa->winsys->displaytarget_get_handle(fsa->winsys, rsc->dt, handle);
}

static void
init_allocator(uint64_t size)
{
   falloc_init(0, size, NULL);
   /* Reserve space for text, data, bss */
   /* Currently no check if the program exceed this size */
   falloc_addr_malloc(0, 0x30000);
   /* Reserve space for stack */
   falloc_addr_malloc(0x20000000 - 0x100 * 64, 0x100 * 64);
}

struct pipe_screen *
fsa_create_screen(struct sw_winsys *winsys)
{
   fsa_debug_init();
   FSA_DBG(FSA_DEBUG_SCREEN, "fsa_create_screen\n");
   struct fsa_screen *screen = CALLOC_STRUCT(fsa_screen);
   if (!screen)
      return NULL;

   fsa_llvm_riscv_init();
   /* Emulator setup */
   uint64_t emu_mem_size = 0x20000000ULL;
   screen->emu = gfxemu_create(emu_mem_size, FSA_DEBUG & FSA_DEBUG_EMU_VS,
                               FSA_DEBUG & FSA_DEBUG_EMU_FS);
   init_allocator(emu_mem_size);

   fsa_init_screen_caps(screen);
   fsa_init_shader_caps(screen);

   screen->winsys = winsys;

   screen->base.destroy = fsa_screen_destroy;
   screen->base.get_name = fsa_screen_get_name;
   screen->base.get_vendor = fsa_screen_get_vendor;
   screen->base.is_format_supported = fsa_screen_is_format_supported;
   screen->base.get_compiler_options = fsa_get_compiler_options;

   screen->base.fence_reference = fsa_fence_reference;
   screen->base.fence_finish = fsa_fence_finish;

   screen->base.flush_frontbuffer = fsa_flush_frontbuffer;
   screen->base.resource_get_handle = fsa_resource_get_handle;

   screen->base.resource_create = fsa_emu_resource_create;
   screen->base.resource_destroy = fsa_emu_resource_destroy;

   screen->base.context_create = fsa_create_context;

   return &screen->base;
}
