#ifndef FSA_SAMPLER_VIEW_H
#define FSA_SAMPLER_VIEW_H

#include "pipe/p_state.h"

struct fsa_sampler_view {
   struct pipe_sampler_view base;
};

static inline struct fsa_sampler_view *
fsa_sampler_view(struct pipe_sampler_view *psam)
{
   return (struct fsa_sampler_view *)psam;
}

#endif