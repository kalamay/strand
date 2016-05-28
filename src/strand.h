#ifndef STRAND_TASK_H
#define STRAND_TASK_H

#include <siphon/common.h>

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

SP_EXPORT const StrandConfig *const STRAND_CONFIG;
SP_EXPORT const StrandConfig *const STRAND_DEFAULT;
SP_EXPORT const StrandConfig *const STRAND_DEBUG;

/**
 * Updates the configuration for subsequent coroutines
 *
 * Initially, coroutines are created using the `STRAND_DEFAULT` configuration.
 * This only needs to be called if the configuration needs to be changed.
 * All threads use the same configuration, and configuration is lock-free and thread-safe.
 * Currently active coroutines will not be changed.
 *
 * @param  cfg  configuration pointer
 */
SP_EXPORT void
strand_configure (const StrandConfig *cfg);

/**
 * Creates a new coroutine with a function for execution context
 *
 * The newly created coroutine will be in a suspended state. Calling
 * `strand_resume` on the returned value will transfer execution context
 * to the function `fn`.
 *
 * @param  fn    the function to execute in the new context
 * @param  data  user pointer to associate with the coroutine
 * @returns  new coroutine or `NULL` on error
 */
SP_EXPORT Strand *
strand_new (uintptr_t (*fn) (void *data, uintptr_t val), void *data);

/**
 * Frees an inactive coroutine
 *
 * This returns the memory allocated for the coroutine. This may not actually
 * return the memory to the OS, and the stack may be reused later.
 *
 * `sp` cannot be `NULL`, but `*sp` may be.
 *
 * @param  sp  reference to the coroutine pointer to free
 */
SP_EXPORT void
strand_free (Strand **sp);

SP_EXPORT size_t
strand_stack_used (void);

SP_EXPORT size_t
strand_stack_used_of (const Strand *s);

/**
 * Checks if a coroutine is not dead
 *
 * This returns `true` if the state is suspended, current, or active. In
 * other words, this is similar to:
 *
 *     strand_state_of (s) != STRAND_DEAD
 *
 * The one difference is that a `NULL` coroutine is not considered alive,
 * whereas `strand_state_of` expects a non-NULL coroutine.
 *
 * @param  s  the coroutine to test or `NULL`
 * @returns  `true` if alive, `false` if dead
 */
SP_EXPORT bool
strand_alive (const Strand *s);

/**
 * Gets the state of a coroutine
 *
 * @param  s  the coroutine to test or `NULL` for current
 * @returns  state value
 */
SP_EXPORT StrandState
strand_state_of (const Strand *s);

/**
 * Gives context to en explicit coroutine
 *
 * If this is the first activation of the coroutine, `val` will be the input
 * parameter to the coroutine's function. If this is a secondary  activation,
 * `val` will be returned from the `strand_yield` call within the the
 * coroutine's function.
 *
 * The returned value is either the value passed into `strand_yield` or the
 * final returned value from the coroutine function.
 *
 * @param  s    coroutine to activate
 * @param  val  value to pass to the coroutine
 */
SP_EXPORT uintptr_t
strand_resume (Strand *s, uintptr_t val);

/**
 * Gives up context from the current coroutine
 *
 * This will return context to the parent coroutine that called `strand_resume`.
 * `val` will become the return value to `strand_resume`, allowing a value to be
 * communicated back to the calling context.
 *
 * The returned value is the value passed into the context to `strand_resume`.
 *
 * @param  val  value to send to the context
 * @returns  value passed into `strand_resume`
 */
SP_EXPORT uintptr_t
strand_yield (uintptr_t val);

/**
 * Gets the user pointer associated with the active coroutine
 *
 * @returns  user pointer
 */
SP_EXPORT void *
strand_data (void);

/**
 * Gets the user pointer associated with an explicit coroutine
 *
 * @param  s  coroutine to access
 * @returns  user pointer
 */
SP_EXPORT void *
strand_data_of (const Strand *s);

/**
 * Schedules a function to be invoked upon finalization of the active coroutine
 *
 * This will be called after the return of the coroutine function but before
 * yielding back to the parent context. Deferred calls occur in LIFO order.
 *
 * @oaram  fn    function to call
 * @param  data  data to pass to `fn`
 */
SP_EXPORT int
strand_defer (void (*fn) (void *), void *data);

/**
 * Schedules a function to be invoked upon finalization of an explicit coroutine
 *
 * This will be called after the return of the coroutine function but before
 * yielding back to the parent context. Deferred calls occur in LIFO order.
 *
 * @param  s     coroutine to schedule
 * @oaram  fn    function to call
 * @param  data  data to pass to `fn`
 */
SP_EXPORT int
strand_defer_to (Strand *s, void (*fn) (void *), void *data);

/**
 * Gets the allocation-time backtrace for the current coroutine
 *
 * This will return `NULL` if the `capture_backtrace` option was `false`
 * during allocation of the coroutine.
 *
 * @param  s  coroutine to access or `NULL` for current
 * @returns  backtrace string or `NULL`
 */
SP_EXPORT const char *
strand_backtrace (void);

/**
 * Gets the allocation-time backtrace for an explicit coroutine
 *
 * This will return `NULL` if the `capture_backtrace` option was `false`
 * during allocation of the coroutine.
 *
 * @param  s  coroutine to access
 * @returns  backtrace string or `NULL`
 */
SP_EXPORT const char *
strand_backtrace_of (const Strand *s);

#ifdef __BLOCKS__

SP_EXPORT Strand *
strand_new_b (uintptr_t (^block)(uintptr_t val));

SP_EXPORT int
strand_defer_b (void (^block)(void));

SP_EXPORT int
strand_defer_to_b (Strand *s, void (^block)(void));

#endif

#endif

