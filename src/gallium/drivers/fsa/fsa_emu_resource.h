#ifndef FSA_EMU_RESOURCE_H
#define FSA_EMU_RESOURCE_H

#include <stdint.h>
#include "pipe/p_screen.h"
#include "pipe/p_state.h"

struct fsa_emu_resource {
   struct pipe_resource base;

   /* offset into dev base */
   uint64_t dmem_offset;

   /* The distance in bytes between rows of a texture */
   uint32_t stride;

   /* Total size allocated in VRAM */
   uint32_t size;

   struct sw_displaytarget *dt;
};

static inline struct fsa_emu_resource *
fsa_emu_resource(struct pipe_resource *prsc)
{
   return (struct fsa_emu_resource *)prsc;
}

struct pipe_resource *
fsa_emu_resource_create(struct pipe_screen *pipe,
                        const struct pipe_resource *templat);

void fsa_emu_resource_destroy(struct pipe_screen *screen,
                              struct pipe_resource *prsc);
#endif