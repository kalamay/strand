#include "strand.h"
#include "ctx.h"
#include "defer.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <inttypes.h>
#include <assert.h>
#include <errno.h>

#if STRAND_BLOCKS
# define STRAND_FBLOCK (UINT32_C(1) << 31)
#endif

#ifndef MAP_STACK
# define MAP_STACK 0
#endif

/** Bytes required for the corotoutine rounded up to nearest 16 bytes */
#define STRAND_SIZE \
	(sizeof (Strand) + (16 - (sizeof (Strand) % 16)))

/**
 * The stack size in bytes including the extra space below the corotoutine
 *
 * @param  s  coroutine pointer
 * @return  total size of the stack
 */
#define STACK_SIZE(s) \
	((s)->map_size - STRAND_SIZE)

/**
 * Get the mapped address from a coroutine
 *
 * @param  s  coroutine pointer
 * @return  mapped address
 */
#define MAP_BEGIN(s) \
	((uint8_t *)(s) + STRAND_SIZE - (s)->map_size)

#define SUSPENDED 0  /** new created or yielded */
#define CURRENT   1  /** currently has context */
#define ACTIVE    2  /** is in the parent list of the current */
#define DEAD      3  /** function has returned */

/** Maps state integers to name strings */
static const char *state_names[] = {
	[SUSPENDED] = "SUSPENDED",
	[CURRENT]   = "CURRENT",
	[ACTIVE]    = "ACTIVE",
	[DEAD]      = "DEAD",
};

/** Print format string for a Strand pointer */
#define PRI "#<Strand:%012" PRIxPTR " state=%s, stack=%zd>"

/**
 * Expands into the arguments to fill in the `PRI` format specifiers
 *
 * @param  s  coroutine pointer
 * @return  list of arguments
 */
#define PRIARGS(s) \
	(uintptr_t)s, state_names[s->state], strand_stack_used (s)

#define unlikely(x) __builtin_expect(!!(x), 0)

/**
 * Runtime assert that prints stack and error information before aborting
 *
 * @param  s    coroutine pointer
 * @param  exp  expression to ensure is `true`
 * @param  ...  printf-style expression to print before aborting
 */
#define ensure(s, exp, ...) do {                                  \
	if (unlikely (!(exp))) {                                      \
		Strand *s_tmp = (s);                                      \
		if (s_tmp != NULL && s_tmp->backtrace != NULL) {          \
			fprintf (stderr, "error with coroutine " PRI ":\n%s", \
					PRIARGS(s_tmp), s_tmp->backtrace);            \
		}                                                         \
		fprintf (stderr, __VA_ARGS__);                            \
		fputc ('\n', stderr);                                     \
		fflush (stderr);                                          \
		abort ();                                                 \
	}                                                             \
} while (0)

/**
 * Test if the coroutine has debugging enabled
 *
 * @param  s  coroutine pointer
 * @return  if the coroutine is in debug mode
 */
#define debug(s) \
	unlikely ((s)->flags & STRAND_FDEBUG)

struct Strand {
#if defined(__x86_64__)
	uintptr_t ctx[10];
#elif defined(__i386__)
	uintptr_t ctx[7];
#endif
	Strand *parent;
	void *data;
	uintptr_t value;
	StrandDefer *defer;
	char *backtrace;
	uint32_t map_size;
	int state, flags;
#if STRAND_VALGRIND
	unsigned int stack_id;
#endif
};

typedef union {
	int64_t value;
	struct {
		uint32_t stack_size;
		uint32_t flags;
	} cfg;
} StrandConfig;

static __thread Strand top = { .state = CURRENT };
static __thread Strand *current = NULL;
static __thread Strand *dead = NULL;

static StrandConfig config = {
	.cfg = {
		.stack_size = STRAND_STACK_DEFAULT,
		.flags = STRAND_FLAGS_DEFAULT
	}
};

/**
 * Populates a config option with valid values
 *
 * @param  stack_size  desired stack size
 * @param  flags       flags for the coroutine
 * @return  valid configuration value
 */
static StrandConfig
config_make (uint32_t stack_size, uint32_t flags)
{
	if (stack_size > STRAND_STACK_MAX) {
		stack_size = STRAND_STACK_MAX;
	}
	else if (stack_size < STRAND_STACK_MIN) {
		stack_size = STRAND_STACK_MIN;
	}

	return (StrandConfig) {
		.cfg = {
			.stack_size = stack_size,
			.flags = flags & 0x7fffffff
		}
	};
}

/**
 * Reclaims a "freed" mapping
 *
 * If the map size doesn't meet needs it is unmapped and `NULL` is
 * returned. This will not iterate through the dead list as that could be
 * too costly. Perhaps trying a few at a time would be preferrable though?
 *
 * @param  map_size  minimum size requirement for the entire mapping
 * @return  pointer to mapped region or `NULL` if nothing to revive
 */
static uint8_t *
map_revive (uint32_t map_size)
{
	Strand *s = dead;
	if (s == NULL) {
		return NULL;
	}

	uint8_t *map = MAP_BEGIN (s);

	dead = s->parent;
	if (s->map_size < map_size) {
		munmap (map, s->map_size);
		map = NULL;
	}

	return map;
}

/**
 * Reclaims or creates a new mapping
 *
 * @param  map_size  minimum size requirement for the entire mapping
 * @return  pointer to mapped region or `NULL` on error
 */
static uint8_t *
map_alloc (uint32_t map_size)
{
	uint8_t *map = map_revive (map_size);
	if (map == NULL) {
		map = mmap (NULL, map_size, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE|MAP_STACK, -1, 0);
		if (map == MAP_FAILED) {
			map = NULL;
		}
	}
	return map;
}

/**
 * Entry point for a new coroutine
 *
 * This runs the user function, kills the coroutine, and restores the
 * parent context.
 *
 * @param  s   coroutine pointer
 * @param  fn  function for the body of the coroutine
 */
static void
entry (Strand *s, uintptr_t (*fn)(void *, uintptr_t))
{
	Strand *parent = s->parent;
	uintptr_t val = fn (s->data, s->value);

	current = parent;

	s->parent = NULL;
	s->value = val;
	s->state = DEAD;
	parent->state = CURRENT;
	strand_defer_run (&s->defer);
	strand_ctx_swap (s->ctx, parent->ctx);
}

/**
 * Maps a new stack and coroutine region
 *
 * The mapping is a single contiguous region with the coroutine state struct
 * stored at the end and the stack is position just below. If the stack is
 * protected, the last page will be locked. Currently, only stacks that grow
 * down are supported, but this is pretty trivial to accomodate.
 *
 * For downward growing stacks, the mapping layout looks like:
 *
 *     +------+--------------------+--------+
 *     | lock | stack              | strand |
 *     +------+--------------------+--------+
 *
 * For upward growing stack, the mapping would probably look like:
 *
 *     +--------+--------------------+------+
 *     | strand | stack              | lock |
 *     +--------+--------------------+------+
 *
 * @param  cfg   configuration struct value
 * @param  fn    function for the body of the coroutine
 * @param  data  user data pointer
 * @return  initialized coroutine pointer
 */
static Strand *
new (StrandConfig cfg, uintptr_t (*fn)(void *, uintptr_t), void *data)
{
	int page_size = getpagesize ();
	if (page_size < 0) {
		return NULL;
	}

	// round to nearest page with additional page to accomodate the strand object
	uint32_t map_size = (((cfg.cfg.stack_size - 1) / page_size) + 2) * page_size;
	uint8_t *map = NULL;
	Strand *s = NULL;

	if (cfg.cfg.flags & STRAND_FPROTECT) {
		map_size += page_size;
	}
	
	map = map_alloc (map_size);
	if (map == NULL) {
		return NULL;
	}

	s = (Strand *)(map + map_size - STRAND_SIZE);
	assert ((uintptr_t)s % 16 == 0);

	if ((cfg.cfg.flags & STRAND_FPROTECT)
			&& !(s->flags & STRAND_FPROTECT)
			&& mprotect (map, page_size, PROT_NONE) < 0) {
		int err = errno;
		munmap (map, map_size);
		errno = err;
		return NULL;
	}

	strand_ctx_init (s->ctx, map, STACK_SIZE (s),
			(uintptr_t)entry, (uintptr_t)s, (uintptr_t)fn);

	s->parent = NULL;
	s->data = data;
	s->value = 0;
	s->defer = NULL;
	s->backtrace = NULL;
	s->map_size = map_size;
	s->state = SUSPENDED;
	s->flags = cfg.cfg.flags;

#if STRAND_VALGRIND
	s->stack_id = VALGRIND_STACK_REGISTER (map, STACK_SIZE (s));
#endif

	return s;
}



void
strand_configure (uint32_t stack_size, uint32_t flags)
{
	StrandConfig c = config_make (stack_size, flags);
	while (!__sync_bool_compare_and_swap (&config.value, config.value, c.value));
}

Strand *
strand_new (uintptr_t (*fn)(void *, uintptr_t), void *data)
{
	assert (fn != NULL);

	return new (config, fn, data);
}

Strand *
strand_new_config (uint32_t stack_size, uint32_t flags,
		uintptr_t (*fn)(void *, uintptr_t), void *data)
{
	assert (fn != NULL);

	return new (config_make (stack_size, flags), fn, data);
}

void
strand_free (Strand **sp)
{
	assert (sp != NULL);

	Strand *s = *sp;
	if (s == NULL) { return; }

	ensure (s, s->state != CURRENT, "attempting to free current coroutine");
	ensure (s, s->state != ACTIVE, "attempting to free an active coroutine");

	*sp = NULL;

	strand_defer_run (&s->defer);
	free (s->backtrace);

#if STRAND_VALGRIND
	VALGRIND_STACK_DEREGISTER (s->stack_id);
#endif

	s->parent = dead;
	dead = s;
}

uintptr_t
strand_yield (uintptr_t val)
{
	Strand *s = current, *p = s->parent;

	ensure (s, p != NULL, "yield attempted outside of coroutine");

	current = p;

	s->parent = NULL;
	s->value = val;
	s->state = SUSPENDED;
	p->state = CURRENT;
	strand_ctx_swap (s->ctx, p->ctx);
	return s->value;
}

uintptr_t
strand_resume (Strand *s, uintptr_t val)
{
	ensure (s, s != NULL, "attempting to resume a null coroutine");
	ensure (s, s->state != CURRENT, "attempting to resume the current coroutine");
	ensure (s, s->state != ACTIVE, "attempting to resume an active coroutine");
	ensure (s, s->state != DEAD, "attempting to resume a dead coroutine");

	Strand *p = current;
	if (p == NULL) {
		p = &top;
	}

	current = s;

	s->parent = p;
	s->value = val;
	s->state = CURRENT;
	p->state = ACTIVE;
	strand_ctx_swap (p->ctx, s->ctx);

	return s->value;
}

bool
strand_alive (const Strand *s)
{
	assert (s != NULL);

	return s->state != DEAD;
}

size_t
strand_stack_used (const Strand *s)
{
	return strand_ctx_stack_size (s->ctx, MAP_BEGIN (s), STACK_SIZE (s));
}

int
strand_defer (void (*fn) (void *), void *data)
{
	return strand_defer_add (&current->defer, fn, data);
}

void *
strand_malloc (size_t size)
{
	return strand_defer_malloc (&current->defer, size);
}

void *
strand_calloc (size_t count, size_t size)
{
	return strand_defer_calloc (&current->defer, count, size);
}

void
strand_print (const Strand *s, FILE *out)
{
	if (out == NULL) {
		out = stdout;
	}

	if (s == NULL) {
		fprintf (out, "#<Strand:(null)>\n");
		return;
	}

	fprintf (out, PRI " {>\n", PRIARGS (s));
	strand_ctx_print (s->ctx, out);
	fprintf (out, "}\n");
}

#if STRAND_BLOCKS

/**
 * The function used for block-based coroutines
 *
 * This assumes that `data` is the user-supplied block, invokes it, and
 * then releases is.
 *
 * @param  data  copied block
 * @param  val   initial coroutine value
 * @return  result of block
 */
static uintptr_t
block_shim (void *data, uintptr_t val)
{
	uintptr_t (^block)(uintptr_t) = data;
	uintptr_t out = block (val);
	Block_release (block);
	return out;
}

Strand *
strand_new_b (uintptr_t (^block)(uintptr_t val))
{
	uintptr_t (^copy) (uintptr_t) = Block_copy (block);
	Strand *s = strand_new (block_shim, copy);

	if (s == NULL) {
		Block_release (copy);
		return NULL;
	}

	s->flags |= STRAND_FBLOCK;

	return s;
}

Strand *
strand_new_config_b (uint32_t stack_size, uint32_t flags,
		uintptr_t (^block)(uintptr_t val))
{
	uintptr_t (^copy) (uintptr_t) = Block_copy (block);
	Strand *s = strand_new_config (stack_size, flags, block_shim, copy);

	if (s == NULL) {
		Block_release (copy);
		return NULL;
	}

	s->flags |= STRAND_FBLOCK;

	return s;
}

int
strand_defer_b (void (^block)(void))
{
	return strand_defer_add_b (&current->defer, block);
}

#endif

