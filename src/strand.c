#include "strand.h"
#include "config.h"
#include "ctx.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <inttypes.h>
#include <assert.h>
#include <errno.h>

#if STRAND_EXECINFO
# include <execinfo.h>
#endif

#if STRAND_BLOCKS
# define STRAND_FBLOCK (UINT32_C(1) << 31)
#endif

#ifndef STRAND_PAGESIZE
# define STRAND_PAGESIZE getpagesize()
#endif

#ifndef MAP_STACK
# define MAP_STACK 0
#endif

/**
 * The stack size in bytes including the extra space below the corotoutine
 * and the protected page
 *
 * @param  s  coroutine pointer
 * @return  total size of the stack
 */
#define STACK_SIZE(s) \
	((s)->map_size - sizeof (Strand))

/**
 * Get the mapped address from a coroutine
 *
 * @param  s  coroutine pointer
 * @return  mapped address
 */
#if STACK_GROWS_UP
# define MAP_BEGIN(s) \
	((uint8_t *)(s))
#else
# define MAP_BEGIN(s) \
	((uint8_t *)(s) + sizeof (Strand) - (s)->map_size)
#endif

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
#define FMT "#<Strand:%012" PRIxPTR " state=%s, stack=%" PRIuMAX ">"

/**
 * Expands into the arguments to fill in the `FMT` format specifiers
 *
 * @param  s  coroutine pointer
 * @return  list of arguments
 */
#define FMTARGS(s) \
	(uintptr_t)s, state_names[s->state], (uintmax_t)strand_stack_used (s)

/**
 * Runtime assert that prints stack and error information before aborting
 *
 * @param  s    coroutine pointer
 * @param  exp  expression to ensure is `true`
 * @param  ...  printf-style expression to print before aborting
 */
#define ensure(s, exp, ...) do {                             \
	if (__builtin_expect (!(exp), 0)) {                      \
		Strand *s_tmp = (s);                                 \
		fprintf (stderr, __VA_ARGS__);                       \
		fprintf (stderr, " (" FMT ")\n", FMTARGS(s_tmp));    \
		for (int i = 0; i < s_tmp->nbacktrace; i++) {        \
			fprintf (stderr, "\t%s\n", s_tmp->backtrace[i]); \
		}                                                    \
		fflush (stderr);                                     \
		abort ();                                            \
	}                                                        \
} while (0)

/**
 * Test if the coroutine has debugging enabled
 *
 * @param  s  coroutine pointer
 * @return  if the coroutine is in debug mode
 */
#define debug(s) \
	__builtin_expect ((s)->flags & STRAND_FDEBUG, 0)

typedef struct StrandDefer StrandDefer;

struct Strand {
	uintptr_t ctx[STRAND_CTX_REG_COUNT];
	Strand *parent;
	void *data;
	uintptr_t value;
	StrandDefer *defer;
	char **backtrace;
	int nbacktrace;
	uint32_t map_size;
	int state, flags;
#if STRAND_VALGRIND
	unsigned int stack_id;
#endif
} __attribute__ ((aligned (16)));

struct StrandDefer {
	StrandDefer *next;
	void (*fn) (void *);
	void *data;
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
static __thread StrandDefer *pool = NULL;

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
map_revive (uint32_t *map_size)
{
	Strand *s = dead;
	if (s == NULL) {
		return NULL;
	}

	uint8_t *map = MAP_BEGIN (s);

	dead = s->parent;
	if (s->map_size < *map_size) {
		munmap (map, s->map_size);
		map = NULL;
	}
	else {
		*map_size = s->map_size;
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
map_alloc (uint32_t *map_size)
{
	uint8_t *map = map_revive (map_size);
	if (map == NULL) {
		map = mmap (NULL, *map_size, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE|MAP_STACK, -1, 0);
		if (map == MAP_FAILED) {
			map = NULL;
		}
	}
	return map;
}

static void
defer_run (StrandDefer **d)
{
	assert (d != NULL);

	StrandDefer *def = *d;
	*d = NULL;

	while (def) {
		StrandDefer *next = def->next;
		def->fn (def->data);
		def->next = pool;
		pool = def;
		def = next;
	}
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
	defer_run (&s->defer);
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
 *     +--------+--------------------+--------+
 *     |  lock  | stack              | strand |
 *     +--------+--------------------+--------+
 *
 * For upward growing stack, the mapping looks like:
 *
 *     +--------+--------------------+--------+
 *     | strand | stack              |  lock  |
 *     +--------+--------------------+--------+
 *
 * @param  cfg   configuration struct value
 * @param  fn    function for the body of the coroutine
 * @param  data  user data pointer
 * @return  initialized coroutine pointer
 */
static Strand *
new (StrandConfig cfg, uintptr_t (*fn)(void *, uintptr_t), void *data)
{
	const int page_size = STRAND_PAGESIZE;
	// round to nearest page with additional page to accomodate the strand object
	uint32_t map_size = (((cfg.cfg.stack_size - 1) / page_size) + 2) * page_size;
	uint8_t *map = NULL, *stack = NULL;
	Strand *s = NULL;

	if (cfg.cfg.flags & STRAND_FPROTECT) {
		map_size += page_size;
	}
	
	map = map_alloc (&map_size);
	if (map == NULL) {
		return NULL;
	}

#if STACK_GROWS_UP
	stack = map + sizeof (Strand);
	s = (Strand *)map;
#else
	stack = map;
	s = (Strand *)(map + map_size - sizeof (Strand));
#endif

	if ((cfg.cfg.flags & STRAND_FPROTECT) && !(s->flags & STRAND_FPROTECT)) {
#if STACK_GROWS_UP
		int rc = mprotect (map+map_size-page_size, page_size, PROT_NONE);
#else
		int rc = mprotect (map, page_size, PROT_NONE);
#endif
		if (rc < 0) {
			int err = errno;
			munmap (map, map_size);
			errno = err;
			return NULL;
		}
	}

	s->parent = NULL;
	s->data = data;
	s->value = 0;
	s->defer = NULL;
	s->backtrace = NULL;
	s->nbacktrace = 0;
	s->map_size = map_size;
	s->state = SUSPENDED;
	s->flags = cfg.cfg.flags;
#if STRAND_VALGRIND
	s->stack_id = VALGRIND_STACK_REGISTER (map, STACK_SIZE (s));
#endif

	strand_ctx_init (s->ctx, stack, STACK_SIZE (s),
			(uintptr_t)entry, (uintptr_t)s, (uintptr_t)fn);

#if STRAND_EXECINFO
	if (cfg.cfg.flags & STRAND_FCAPTURE) {
		void *calls[32];
		int frames = backtrace (calls, sizeof calls / sizeof calls[0]);
		if (frames > 0) {
			s->backtrace = backtrace_symbols (calls, frames);
			s->nbacktrace = frames;
		}
	}
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

	defer_run (&s->defer);
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
	return strand_ctx_stack_size (s->ctx, MAP_BEGIN (s), STACK_SIZE (s), s == current);
}

int
strand_defer (void (*fn) (void *), void *data)
{
	assert (fn != NULL);

	StrandDefer *def = pool;
	if (def != NULL) {
		pool = def->next;
	}
	else {
		def = malloc (sizeof (*def));
		if (def == NULL) {
			return -errno;
		}
	}

	def->next = current->defer;
	def->fn = fn;
	def->data = data;
	current->defer = def;

	return 0;
}

static void *
defer_free (void *val)
{
	if (val != NULL && strand_defer (free, val) < 0) {
		free (val);
		val = NULL;
	}
	return val;
}

void *
strand_malloc (size_t size)
{
	return defer_free (malloc (size));
}

void *
strand_calloc (size_t count, size_t size)
{
	return defer_free (calloc (count, size));
}

void
strand_print (const Strand *s, FILE *out)
{
	if (s == NULL) {
		s = current;
	}

	if (out == NULL) {
		out = stdout;
	}

	fprintf (out, FMT " {\n", FMTARGS (s));
	strand_ctx_print (s->ctx, out);
	if (s->nbacktrace > 0) {
		fprintf (out, "\tbacktrace:\n");
		for (int i = 0; i < s->nbacktrace; i++) {
			fprintf (stderr, "\t\t%s\n", s->backtrace[i]);
		}
	}
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

static void
defer_shim (void *data)
{
	void (^block)(void) = data;
	block ();
	Block_release (block);
}

int
strand_defer_b (void (^block)(void))
{
	void (^copy) (void) = Block_copy (block);
	int rc = strand_defer (defer_shim, copy);
	if (rc < 0) {
		Block_release (copy);
	}
	return rc;
}

#endif

