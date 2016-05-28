#ifndef STRAND_DEFER_H
#define STRAND_DEFER_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

typedef struct StrandDefer StrandDefer;

extern int
strand_defer_add (StrandDefer **d, void (*fn) (void *), void *data);

extern void
strand_defer_run (StrandDefer **defp);

#ifdef __BLOCKS__

extern int
strand_defer_add_b (StrandDefer **d, void (^block)(void));

#endif

#endif

