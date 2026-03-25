#include "fsa_emu_resource.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "falloc.h"
#include "fsa_debug.h"
#include "fsa_screen.h"

struct pipe_resource *
fsa_emu_resource_create(struct pipe_screen *screen,
                        const struct pipe_resource *templat)
{
   FSA_DBG(FSA_DEBUG_RESOURCE, "fsa_emu_resource_create\n");
   struct fsa_emu_resource *rsc = CALLOC_STRUCT(fsa_emu_resource);
   if (!rsc) {
      return NULL;
   }
   rsc->base = *templat;
   rsc->base.screen = screen;

   pipe_reference_init(&rsc->base.reference, 1);

   uint32_t size = 0;

   if (templat->target == PIPE_BUFFER) {
      size = templat->width0;
      rsc->stride = 0;
   } else {
      /* 2D linear */
      uint32_t bytes_per_pixel = util_format_get_blocksize(templat->format);
      rsc->stride = templat->width0 * bytes_per_pixel;
      size = rsc->stride * templat->height0;
   }

   if (templat->bind & (PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_SCANOUT)) {
      struct fsa_screen *fscreen = fsa_screen(screen);
      rsc->dt = fscreen->winsys->displaytarget_create(
         fscreen->winsys, templat->bind, templat->format, templat->width0,
         templat->height0, 64, NULL, &rsc->stride);
   }

   if (templat->target != PIPE_BUFFER) {
      size = rsc->stride * templat->height0;
   } else {
      size = templat->width0;
   }
   rsc->size = align(size, 64);

   int res = falloc_malloc((void **)&rsc->dmem_offset, rsc->size);

   /* OOM */
   if (res) {
      if (rsc->dt) {
         struct fsa_screen *fsa = fsa_screen(screen);
         fsa->winsys->displaytarget_destroy(fsa->winsys, rsc->dt);
      }
      FREE(rsc);
      return NULL;
   }
   return &rsc->base;
}

void
fsa_emu_resource_destroy(struct pipe_screen *screen, struct pipe_resource *prsc)
{
   FSA_DBG(FSA_DEBUG_RESOURCE, "fsa_emu_resource_destroy\n");
   struct fsa_emu_resource *rsc = fsa_emu_resource(prsc);
   if (rsc->dt) {
      struct fsa_screen *fsa = fsa_screen(screen);
      fsa->winsys->displaytarget_destroy(fsa->winsys, rsc->dt);
   }
   falloc_free((void *)rsc->dmem_offset);
   FREE(rsc);
}