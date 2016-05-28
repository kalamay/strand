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
#ifdef __BLOCKS__
	bool block;
#endif
};

union StrandConfig {
	StrandConfig config;
	uint64_t value;
};

static union StrandConfig config = {
	.config = {
		.stack_size = 65536,
		.protect = true,
		.capture_backtrace = false
	}
};

#define STACK_SIZE (config.config.stack_size)
#define CAPTURE_BACKTRACE (config.config.capture_backtrace)
#define PROTECT (config.config.protect)

static const StrandConfig config_default = {
	.stack_size = 65536,
	.protect = true,
	.capture_backtrace = false
};

static const StrandConfig config_debug = {
	.stack_size = 65536,
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

#define STACK_OFFSET \
	(sizeof (Strand) + (16 - (sizeof (Strand) % 16)))

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
strand_revive (size_t stack_size)
{
	Strand *s = dead;
	if (s != NULL) {
		dead = s->parent;
		if (s->stack_size < stack_size) {
			munmap (s->stack, s->stack_size);
			s = NULL;
		}
	}
	return s;
}

static Strand *
strand_alloc (size_t stack_size, size_t page_size)
{
	assert (stack_size % page_size == 0);

	uint8_t *stack;
	Strand *s;

	stack = mmap (NULL, stack_size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
	if (stack == MAP_FAILED) {
		goto err_stack;
	}

	if (PROTECT && mprotect (stack, page_size, PROT_NONE) < 0) {
		goto err_protect;
	}

	s = (Strand *)((stack + stack_size) - STACK_OFFSET);
	assert ((uintptr_t)s % 16 == 0);

	s->stack = stack;
	s->stack_size = stack_size;

	return s;

err_protect:
	munmap (stack, stack_size);
err_stack:
	return NULL;
}

static Strand *
strand_new_empty (void)
{
	size_t page_size = getpagesize ();
	size_t stack_size = sp_next_quantum (STACK_SIZE, page_size) + page_size;
	if (PROTECT) {
		stack_size += page_size;
	}

	Strand *s = strand_revive (stack_size);
	if (s == NULL) {
		s = strand_alloc (stack_size, page_size);
		if (s == NULL) {
			return NULL;
		}
	}

	s->parent = NULL;
	s->data = NULL;
	s->value = 0;
	s->defer = NULL;
	s->state = STRAND_SUSPENDED;
#ifdef __BLOCKS__
	s->block = false;
#endif

	if (CAPTURE_BACKTRACE) {
		char buf[1024];
		ssize_t len = sp_stack (buf, sizeof buf);
		if (len > 0) {
			s->backtrace = strndup (buf, (size_t)len);
		}
	}

	return s;
}

Strand *
strand_new (uintptr_t (*fn) (void *, uintptr_t), void *data)
{
	Strand *s = strand_new_empty ();
	if (s != NULL) {
		s->ctx.rdi = (uintptr_t)s;
		s->ctx.rsi = (uintptr_t)fn;
		s->ctx.rip = (uintptr_t)entry;
		s->ctx.rsp = (uintptr_t)((uint8_t *)s - sizeof (uintptr_t));
		s->data = data;
	}
	return s;
}

void
strand_free (Strand **sp)
{
	assert (sp != NULL);

	Strand *s = *sp;
	if (s == NULL) { return; }
	*sp = NULL;

	ensure (s->state != STRAND_CURRENT, "attempting to free active coroutine");

	strand_defer_run (&s->defer);
	free (s->backtrace);
	s->parent = dead;
	dead = s;
}

size_t
strand_stack_used (void)
{
	return strand_stack_used_of (active);
}

size_t
strand_stack_used_of (const Strand *s)
{
	if (s == NULL) {
		return 0;
	}
	return s->stack_size
		- ((uint8_t *)s->ctx.rsp - (uint8_t *)s->stack)
		- STACK_OFFSET;
}

bool
strand_alive (const Strand *s)
{
	return s && (s->state != STRAND_DEAD);
}

StrandState
strand_state_of (const Strand *s)
{
	assert (s != NULL);

	return s->state;
}

uintptr_t
strand_resume (Strand *s, uintptr_t val)
{
	ensure (s != NULL, "attempting to resume a null coroutine");
	ensure (s != active, "attempting to resume a active coroutine");
	ensure (s->state != STRAND_DEAD, "attempting to resume a dead coroutine");

	if (sp_unlikely (active == NULL)) {
		active = &top;
	}

	s->parent = active;
    s->value = val;

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
strand_yield (uintptr_t val)
{
	Strand *s = active;
	ensure (s->parent != NULL, "yield attempted outside of coroutine");

	yield (s, val, STRAND_SUSPENDED);
	return s->value;
}

void *
strand_data (void)
{
	return strand_data_of (active);
}

void *
strand_data_of (const Strand *s)
{
	return s != NULL ? s->data : NULL;
}

int
strand_defer (void (*fn) (void *), void *data)
{
	return strand_defer_to (active, fn, data);
}

int
strand_defer_to (Strand *s, void (*fn) (void *), void *data)
{
	if (s == NULL) {
		return -EINVAL;
	}
	return strand_defer_add (&s->defer, fn, data);
}

const char *
strand_backtrace (void)
{
	return strand_backtrace_of (active);
}

const char *
strand_backtrace_of (const Strand *s)
{
	return s != NULL ? s->backtrace : NULL;
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
		strand_free (&s);
	}

	s->block = true;
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
	if (s == NULL) {
		return -EINVAL;
	}
	return strand_defer_add_b (&s->defer, block);
}

#endif

