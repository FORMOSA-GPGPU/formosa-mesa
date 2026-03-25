#ifndef FSA_SAMPLER_STATE_H
#define FSA_SAMPLER_STATE_H

#include "pipe/p_state.h"

struct fsa_sampler_state {
   struct pipe_sampler_state base;
};

static inline struct fsa_sampler_state *
fsa_sampler_state(struct pipe_sampler_state *psam)
{
   return (struct fsa_sampler_state *)psam;
}
#endif