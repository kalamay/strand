#ifndef STRAND_CTX_H
#define STRAND_CTX_H

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

#include "config.h"

#if __SIZEOF_POINTER__ == 8
# define FMTxREG "0x%016" PRIxPTR
#elif __SIZEOF_POINTER__ == 4
# define FMTxREG "0x%08" PRIxPTR
#endif

/**
 * Configures the context to invoke a function with 2 arguments
 *
 * @param  ctx    context pointer
 * @param  stack  lowest address of the stack
 * @param  len    byte length of the stack
 * @param  ip     function to execute
 * @param  a1     first argument to `ip`
 * @param  a2     second argument to `ip`
 */
static void
strand_ctx_init (uintptr_t *ctx, void *stack, size_t len,
		uintptr_t ip, uintptr_t a1, uintptr_t a2);

/**
 * Gets the current stack space usage in bytes
 *
 * @param  ctx      context pointer
 * @param  stack    lowest address of the stack
 * @param  len      byte length of the stack
 * @param  current  is the context currently active
 * @return  number of bytes
 */
static size_t
strand_ctx_stack_size (const uintptr_t *ctx, void *stack, size_t len, bool current);

/**
 * Prints the value of the context
 *
 * @param  ctx  context pointer
 * @param  out  output `FILE *` object
 */
static void
strand_ctx_print (const uintptr_t *ctx, FILE *out);

/**
 * Swaps execution contexts
 *
 * @param  save  destination to save current context
 * @param  load  context to activate
 */
void
strand_ctx_swap (uintptr_t *save, const uintptr_t *load);

#if STRAND_X86_64
# include "ctx/x86_64.c"
#elif STRAND_X86_32
# include "ctx/x86_32.c"
#endif

#endif

