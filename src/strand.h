#ifndef STRAND_TASK_H
#define STRAND_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

typedef struct Strand Strand;

typedef enum {
	STRAND_SUSPENDED,
	STRAND_CURRENT,
	STRAND_ACTIVE,
	STRAND_DEAD
} StrandState;

typedef struct {
	uint32_t stack_size;
	bool protect;
	bool capture_backtrace;
} StrandConfig;

extern const StrandConfig *const STRAND_CONFIG;
extern const StrandConfig *const STRAND_DEFAULT;
extern const StrandConfig *const STRAND_DEBUG;

extern void
strand_configure (const StrandConfig *cfg);

extern Strand *
strand_new (uintptr_t (*fn) (void *data, uintptr_t val), void *data);

extern void
strand_free (Strand *s);

extern bool
strand_alive (const Strand *s);

extern uintptr_t
strand_resume (Strand *s, uintptr_t value);

extern uintptr_t
strand_yield (uintptr_t value);

extern int
strand_defer (void (*fn) (void *), void *data);

extern int
strand_defer_to (Strand *s, void (*fn) (void *), void *data);

extern const char *
strand_backtrace (Strand *s);

#ifdef __BLOCKS__

extern Strand *
strand_new_b (uintptr_t (^block)(uintptr_t val));

extern int
strand_defer_b (void (^block)(void));

extern int
strand_defer_to_b (Strand *s, void (^block)(void));

#endif

#endif

