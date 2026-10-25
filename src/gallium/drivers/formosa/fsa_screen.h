#ifndef FSA_SCREEN_H
#define FSA_SCREEN_H

#include "frontend/sw_winsys.h"
#include "pipe/p_screen.h"
#include "uscsim/cbind.h"

#define GFXEMU_FB_WIDTH  640
#define GFXEMU_FB_HEIGHT 480

struct fsa_screen {
   struct pipe_screen base;
   struct sw_winsys *winsys;
   GfxEmu *emu;
};

static inline struct fsa_screen *
fsa_screen(struct pipe_screen *pscreen)
{
   return (struct fsa_screen *)pscreen;
}

struct pipe_screen *fsa_create_screen(struct sw_winsys *winsys);

#endif /* FSA_SCREEN_H */