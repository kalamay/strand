#ifndef STRAND_CTX_H
#define STRAND_CTX_H

#include "config.h"

#if STRAND_AMD64
typedef struct {
	uintptr_t rbx, rbp, r12, r13, r14, r15, rdi, rsi, rip, rsp;
} StrandContext;
#endif

/**
 * Transfer execution contexts
 *
 * Current registers will be copied into `save`, and registers will be copied
 * from `load`. While functionally equivalent, this is slightly faster than
 * using `strand_save` and `strand_load`.
 *
 * @param  save  context to halt
 * @param  load  context to resume
 */
SP_EXPORT void
strand_swap (StrandContext *save, StrandContext *load);

/**
 * Save current execution context
 *
 * Current registers will be copied into `ctx`. This is used to create a saved
 * point of execution.
 *
 * @param  ctx  context to save into
 */
SP_EXPORT void
strand_save (StrandContext *ctx);

/**
 * Replace current execution context
 *
 * Copies registers from `ctx`. This is used to restored a saved point of
 * execution.
 *
 * @param  ctx  context to load from
 */
SP_EXPORT void
strand_load (StrandContext *ctx);

#endif

