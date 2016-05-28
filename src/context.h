#ifndef STRAND_CTX_H
#define STRAND_CTX_H

#include "config.h"

#if STRAND_AMD64
typedef struct {
	uintptr_t rbx, rbp, r12, r13, r14, r15, rdi, rsi, rip, rsp;
} StrandContext;
#endif

extern void
strand_swap (StrandContext *save, StrandContext *load);

extern void
strand_save (StrandContext *ctx);

extern void
strand_load (StrandContext *ctx);

#endif

