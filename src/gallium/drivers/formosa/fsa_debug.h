#ifndef FSA_DEBUG_H
#define FSA_DEBUG_H

#include "util/u_debug.h"

#define FSA_DEBUG_SCREEN   0x1
#define FSA_DEBUG_CONTEXT  0x2
#define FSA_DEBUG_DRAW     0x4
#define FSA_DEBUG_RESOURCE 0x8
#define FSA_DEBUG_SHADER   0x10
#define FSA_DEBUG_NIR      0x20
#define FSA_DEBUG_ASM      0x40
#define FSA_DEBUG_EMU_VS   0x80
#define FSA_DEBUG_EMU_FS   0x100
#define FSA_DEBUG_ALL      (FSA_DEBUG_NIR - 1)

extern int FSA_DEBUG;

void fsa_debug_init(void);

static inline void
FSA_DBG(unsigned flag, const char *fmt, ...)
{
   if (FSA_DEBUG & flag) {
      va_list args;
      va_start(args, fmt);
      debug_vprintf(fmt, args);
      va_end(args);
   }
}

#endif /* FSA_DEBUG_H */
