#include "strand.h"
#include "context.h"
#include "defer.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <stdio.h>
#include <assert.h>
#include <sys/mman.h>
#include <signal.h>
#ifdef __BLOCKS__
#include <Block.h>
#endif

#include <siphon/common.h>
#include <siphon/error.h>

#define ensure(exp, ...) do {    \
	if (sp_unlikely (!(exp))) {  \
		sp_fabort (__VA_ARGS__); \
	}                            \
} while (0)

struct Strand {
	StrandContext ctx;
	Strand *parent;
	void *stack;
	size_t stack_size;
	void *data;
	uintptr_t value;
	StrandDefer *defer;
	char *backtrace;
	StrandState state;
};

union StrandConfig {
	StrandConfig config;
	uint64_t value;
};

static union StrandConfig config = {
	.config = {
		.stack_size = 16384,
		.protect = true,
		.capture_backtrace = false
	}
};

#define STACK_SIZE (config.config.stack_size)
#define CAPTURE_BACKTRACE (config.config.capture_backtrace)
#define PROTECT (config.config.protect)

static const StrandConfig config_default = {
	.stack_size = 16384,
	.protect = true,
	.capture_backtrace = false
};

static const StrandConfig config_debug = {
	.stack_size = 16384,
	.protect = true,
	.capture_backtrace = true
};

const StrandConfig *const STRAND_CONFIG = &config.config;
const StrandConfig *const STRAND_DEFAULT = &config_default;
const StrandConfig *const STRAND_DEBUG = &config_debug;

static __thread StrandContext caller, callee;
static __thread Strand top = { .state = STRAND_CURRENT };
static __thread Strand *active = NULL;
static __thread Strand *dead = NULL;

#ifdef STRAND_STACK_CHECK

static int stack_order = -1;

static void
stack_check_from (int *first)
{
	int second;
	stack_order = first < &second ? 1 : -1;
}

static void __attribute__((constructor))
stack_check (void)
{
	int first;
	stack_check_from (&first);
}

#endif

static inline void __attribute__((always_inline))
yield (Strand *s, uintptr_t val, StrandState state)
{
    s->value = val;
	s->state = state;
	s->parent->state = STRAND_CURRENT;
	active = s->parent;
	s->parent = NULL;

    strand_swap (&callee, &caller);
}

static void
entry (Strand *s, uintptr_t (*fn) (void *, uintptr_t))
{
	uintptr_t rv = fn (s->data, s->value);
	yield (s, rv, STRAND_DEAD);
}

void
strand_configure (const StrandConfig *cfg)
{
	union StrandConfig c;
	c.config = *cfg;

	while (!__sync_bool_compare_and_swap (&config.value, config.value, c.value));
}

static Strand *
strand_alloc (void)
{
	uint8_t *stack;
	Strand *s;
	size_t page = getpagesize ();
	size_t stack_size = sp_next_quantum (STACK_SIZE, page) + page;
	if (PROTECT) {
		stack_size += page;
	}

	stack = mmap (NULL, stack_size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
	if (stack == MAP_FAILED) {
		goto err_stack;
	}

	if (PROTECT && mprotect (stack, page, PROT_NONE) < 0) {
		goto err_protect;
	}

	s = (Strand *)((stack + stack_size) - sizeof *s - (16 - (sizeof *s % 16)));
	assert ((uintptr_t)s % 16 == 0);

	s->stack = stack;
	s->stack_size = stack_size;

	return s;

err_protect:
	munmap (stack, stack_size);
err_stack:
	return NULL;
}

Strand *
strand_new (uintptr_t (*fn) (void *, uintptr_t), void *data)
{
	assert (fn != NULL);

	Strand *s = dead;
	if (s == NULL) {
		s = strand_alloc ();
		if (s == NULL) {
			return NULL;
		}
	}
	else {
		dead = s->parent;
	}

	s->ctx.rdi = (uintptr_t)s;
	s->ctx.rsi = (uintptr_t)fn;
	s->ctx.rip = (uintptr_t)entry;
	s->ctx.rsp = (uintptr_t)((uint8_t *)s - sizeof (uintptr_t));
	s->parent = NULL;
	s->data = data;
	s->value = 0;
	s->defer = NULL;
	s->backtrace = NULL;
	s->state = STRAND_SUSPENDED;

	if (CAPTURE_BACKTRACE) {
		char buf[1024];
		ssize_t len = sp_stack (buf, sizeof buf);
		if (len > 0) {
			s->backtrace = strndup (buf, (size_t)len);
		}
	}

	return s;
}

void
strand_free (Strand *s)
{
	if (s == NULL) { return; }

	ensure (s->state != STRAND_CURRENT, "attempting to free active strand");

	strand_defer_run (&s->defer);
	free (s->backtrace);
	s->parent = dead;
	dead = s;
}

bool
strand_alive (const Strand *s)
{
	return s && (s->state != STRAND_DEAD);
}

uintptr_t
strand_resume (Strand *s, uintptr_t value)
{
	ensure (s->state != STRAND_DEAD, "attempting to resume a dead strand");

	if (sp_unlikely (active == NULL)) {
		active = &top;
	}

	s->parent = active;
    s->value = value;

	active = s;

	s->parent->state = STRAND_ACTIVE;
	s->state = STRAND_CURRENT;

	strand_swap (&caller, &s->ctx);

	if (s->state != STRAND_DEAD) {
		memcpy (&s->ctx, &callee, sizeof callee);
	}
	else {
		strand_defer_run (&s->defer);
	}
	return s->value;
}

uintptr_t
strand_yield (uintptr_t value)
{
	Strand *s = active;
	ensure (s->parent != NULL, "yield attempted outside of strand");

	yield (s, value, STRAND_SUSPENDED);
	return s->value;
}

int
strand_defer (void (*fn) (void *), void *data)
{
	return strand_defer_to (active, fn, data);
}

int
strand_defer_to (Strand *s, void (*fn) (void *), void *data)
{
	return strand_defer_add (&s->defer, fn, data);
}

const char *
strand_backtrace (Strand *s)
{
	return s->backtrace;
}

#ifdef __BLOCKS__

static uintptr_t
block_shim (void *data, uintptr_t val)
{
	uintptr_t (^block)(uintptr_t) = data;
	return block (val);
}

static void
block_shim_defer (void *data)
{
	Block_release (data);
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

	if (strand_defer_to (s, block_shim_defer, copy) < 0) {
		Block_release (copy);
		strand_free (s);
		return NULL;
	}

	return s;
}

int
strand_defer_b (void (^block)(void))
{
	return strand_defer_to_b (active, block);
}

int
strand_defer_to_b (Strand *s, void (^block)(void))
{
	return strand_defer_add_b (&s->defer, block);
}

#endif

