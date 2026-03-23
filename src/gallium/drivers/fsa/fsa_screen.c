#include "fsa_screen.h"
#include <stdio.h>
#include "compiler/nir/nir_shader_compiler_options.h"
#include "pipe/p_defines.h"
#include "util/format/u_format.h"
#include "util/u_memory.h"
#include "util/u_screen.h"
#include "fsa_context.h"
#include "fsa_emu_resource.h"

static const char *
fsa_screen_get_name(struct pipe_screen *pscreen)
{
   return "GFXEmu";
}

static const char *
fsa_screen_get_vendor(struct pipe_screen *pscreen)
{
   return "Formosa";
}

static void
fsa_screen_destroy(struct pipe_screen *pscreen)
{
   struct fsa_screen *screen = fsa_screen(pscreen);
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
   if (target != PIPE_BUFFER && target != PIPE_TEXTURE_2D) {
      return false;
   }

   /* Z* : depth S* : stencil */
   if (bindings & PIPE_BIND_DEPTH_STENCIL) {
      if (!util_format_is_depth_or_stencil(format))
         return false;
      switch (format) {
      case PIPE_FORMAT_Z32_FLOAT:
         break;
      default:
         return false;
      }
   }

   /* Color */
   if (bindings & PIPE_BIND_RENDER_TARGET) {
      if (util_format_is_depth_or_stencil(format))
         return false;

      switch (format) {
      case PIPE_FORMAT_B8G8R8A8_UNORM:
         break;
      default:
         return false;
      }
   }

   /* Not sure what this is */
   if (bindings & PIPE_BIND_SAMPLER_VIEW) {
      if (format != PIPE_FORMAT_B8G8R8A8_UNORM) {
         return false;
      }
   }

   /* Scanout / Display */
   if (bindings & PIPE_BIND_DISPLAY_TARGET) {
      if (format != PIPE_FORMAT_B8G8R8A8_UNORM) {
         return false;
      }
   }
   return true;
}

/* Temp copy from `fsa_compile.c`, merge the two in future changes */
static const nir_shader_compiler_options _fsa_nir_option = {
   .lower_pack_half_2x16 = true,
   .lower_pack_unorm_2x16 = true,
   .lower_pack_snorm_2x16 = true,
   .lower_pack_unorm_4x8 = true,
   .lower_pack_snorm_4x8 = true,
   .lower_pack_64_2x32 = true,
   .lower_pack_64_4x16 = true,
   .lower_pack_32_2x16 = true,
   .lower_pack_64_2x32_split = true,
   .lower_pack_32_2x16_split = true,
   .lower_unpack_half_2x16 = true,
   .lower_unpack_unorm_2x16 = true,
   .lower_unpack_snorm_2x16 = true,
   .lower_unpack_unorm_4x8 = true,
   .lower_unpack_snorm_4x8 = true,
   .lower_unpack_64_2x32_split = true,
   .lower_unpack_32_2x16_split = true,
};

static const void *
fsa_get_compiler_options(struct pipe_screen *screen, enum pipe_shader_ir ir,
                         enum pipe_shader_type shader)
{
   if (ir != PIPE_SHADER_IR_NIR) {
      return NULL;
   }
   return &_fsa_nir_option;
}

static void
fsa_init_screen_caps(struct fsa_screen *screen)
{
   struct pipe_caps *caps = (struct pipe_caps *)&screen->base.caps;

   u_init_pipe_screen_caps(&screen->base, 1);

   caps->npot_textures = true;
   caps->blend_equation_separate = true;
}

static void
fsa_init_shader_caps(struct fsa_screen *screen)
{
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
   }
}

static void
fsa_fence_reference(struct pipe_screen *screen, struct pipe_fence_handle **ptr,
                    struct pipe_fence_handle *fence)
{
   *ptr = fence;
}

static bool
fsa_fence_finish(struct pipe_screen *screen, struct pipe_context *ctx,
                 struct pipe_fence_handle *fence, uint64_t timeout)
{
   return true;
}

static void
fsa_flush_frontbuffer(struct pipe_screen *screen, struct pipe_context *ctx,
                      struct pipe_resource *resource, unsigned level,
                      unsigned layer, void *winsys_drawable_handle,
                      unsigned nboxes, struct pipe_box *subbox)
{
   struct fsa_screen *fsa = fsa_screen(screen);
   struct fsa_emu_resource *rsc = fsa_emu_resource(resource);
   if (rsc->dt) {
      void *host_map =
         fsa->winsys->displaytarget_map(fsa->winsys, rsc->dt, PIPE_MAP_WRITE);
      if (host_map) {
         /* TODO: memcpy from sim vram to display target */
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
   struct fsa_screen *fsa = fsa_screen(screen);
   struct fsa_emu_resource *rsc = fsa_emu_resource(tex);
   if (!rsc->dt)
      return false;
   return fsa->winsys->displaytarget_get_handle(fsa->winsys, rsc->dt, handle);
}

struct pipe_screen *
fsa_create_screen(struct sw_winsys *winsys)
{
   printf("[FSA] Create screen\n");
   struct fsa_screen *screen = CALLOC_STRUCT(fsa_screen);
   if (!screen)
      return NULL;

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
