#ifndef FSA_TRANSFER_H
#define FSA_TRANSFER_H

#include "pipe/p_state.h"

struct fsa_transfer {
   struct pipe_transfer base;

   /* Staging buffer ready for transfer to VRAM */
   void *staging;
};

static inline struct fsa_transfer *
fsa_transfer(struct pipe_transfer *ptransfer)
{
   return (struct fsa_transfer *)ptransfer;
}

#endif