#ifndef STRAND_H
#define STRAND_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

#define STRAND_FDEBUG   (UINT32_C(1) << 0) /** enable debug statements */
#define STRAND_FPROTECT (UINT32_C(1) << 1) /** protect the end of the stack */
#define STRAND_FCAPTURE (UINT32_C(1) << 2) /** capture stack for new coroutines */

/**
 * Minimum allowed stack size
 */
#define STRAND_STACK_MIN 16384

/**
 * Maximum allowed stack size
 */
#define STRAND_STACK_MAX (1024 * STRAND_STACK_MIN)

/**
 * Stack size just large enough to call all of glibc functions
 */
#define STRAND_STACK_DEFAULT (8 * STRAND_STACK_MIN)

/**
 * Flag combination ideal for general use
 */
#define STRAND_FLAGS_DEFAULT (STRAND_FPROTECT)

/**
 * Flag combination ideal for debugging purposed
 */
#define STRAND_FLAGS_DEBUG (STRAND_FPROTECT | STRAND_FDEBUG | STRAND_FCAPTURE)

/**
 * Opaque type for coroutine instances
 */
typedef struct Strand Strand;

/**
 * Updates the configuration for subsequent coroutines.
 *
 * Initially, coroutines are created using the `STRAND_STACK_DEFAULT` stack
 * size and the in `STRAND_FLAGS_DEFAULT` mode. This only needs to be called
 * if the configuration needs to be changed. All threads use the same
 * configuration, and configuration is lock-free and thread-safe.
 *
 * Currently active coroutines will not be changed.
 *
 * @param  stack_size  minimun stack size allocated
 * @param  flags       configuration flags
 */
extern void
strand_configure (uint32_t stack_size, uint32_t flags);

/**
 * Creates a new coroutine with a function for execution context
 *
 * The newly created coroutine will be in a suspended state. Calling
 * `strand_resume` on the returned value will transfer execution context
 * to the function `fn`.
 *
 * @param  fn    the function to execute in the new context
 * @param  data  user pointer to associate with the coroutine
 * @return  new coroutine or `NULL` on error
 */
extern Strand *
strand_new (uintptr_t (*fn)(void *, uintptr_t), void *data);

/**
 * Creates a new coroutine with a function for execution context using
 * non-global configuration options.
 *
 * The newly created coroutine will be in a suspended state. Calling
 * `strand_resume` on the returned value will transfer execution context
 * to the function `fn`.
 *
 * @param  stack_size  the minimum size to create for the new context
 * @param  flags       configuration flags for the new context
 * @param  fn          the function to execute in the new context
 * @param  data        user pointer to associate with the coroutine
 * @return  new coroutine or `NULL` on error
 */
extern Strand *
strand_new_config (uint32_t stack_size, uint32_t flags,
		uintptr_t (*fn)(void *, uintptr_t), void *data);

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
extern void
strand_free (Strand **sp);

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
 * @return  value passed into `strand_resume`
 */
extern uintptr_t
strand_yield (uintptr_t val);

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
extern uintptr_t
strand_resume (Strand *s, uintptr_t val);

/**
 * Checks if a coroutine is not dead
 *
 * This returns `true` if the state is suspended, current, or active. In
 * other words, this is similar to:
 *
 *     strand_state (s) != STRAND_DEAD
 *
 * The one difference is that a `NULL` coroutine is not considered alive,
 * whereas `strand_state` expects a non-NULL coroutine.
 *
 * @param  s  the coroutine to test or `NULL`
 * @return  `true` if alive, `false` if dead
 */
extern bool
strand_alive (const Strand *s);

/**
 * Gets the current stack space used
 *
 * @param  s  the coroutine to access
 * @return  number of bytes used
 */
extern size_t
strand_stack_used (const Strand *s);

/**
 * Schedules a function to be invoked upon finalization of the active coroutine
 *
 * This will be called after the return of the coroutine function but before
 * yielding back to the parent context. Deferred calls occur in LIFO order.
 *
 * @param  fn    function to call
 * @param  data  data to pass to `fn`
 */
extern int
strand_defer (void (*fn) (void *), void *data);

/**
 * Creates an allocation that is freed at termination of the coroutine
 *
 * @param  size  number of bytes to allocate
 * @return  point or `NULL` on error
 */
extern void *
strand_malloc (size_t size);

/**
 * Creates a zeroed allocation that is freed at termination of the coroutine
 *
 * @param  count  number of contiguous objects
 * @param  size   number of bytes for each object
 * @return  point or `NULL` on error
 */
extern void *
strand_calloc (size_t count, size_t size);

/**
 * Prints a representation of the coroutine
 *
 * @param  s    the coroutine to print or `NULL`
 * @param  out  `FILE *` handle to write to or `NULL`
 */
extern void
strand_print (const Strand *s, FILE *out);

#if defined (__BLOCKS__)

/**
 * Creates a new coroutine with a block for execution context
 *
 * The newly created coroutine will be in a suspended state. Calling
 * `strand_resume` on the returned value will transfer execution context
 * to `block`.
 *
 * The block will be copied upon creation, and it will be released
 * after exiting.
 *
 * @param  block  the block to execute in the new context
 * @return  new coroutine or `NULL` on error
 */
extern Strand *
strand_new_b (uintptr_t (^block)(uintptr_t val));

/**
 * Creates a new coroutine with a block for execution context using
 * non-global configuration options.
 *
 * The newly created coroutine will be in a suspended state. Calling
 * `strand_resume` on the returned value will transfer execution context
 * to `block`.
 *
 * The block will be copied upon creation, and it will be released
 * after exiting.
 *
 * @param  stack_size  the minimum size to create for the new context
 * @param  flags       configuration flags for the new context
 * @param  block       the block to execute in the new context
 * @return  new coroutine or `NULL` on error
 */
extern Strand *
strand_new_config_b (uint32_t stack_size, uint32_t flags,
		uintptr_t (^block)(uintptr_t val));

/**
 * Schedules a block to be invoked upon finalization of the active coroutine
 *
 * This will be called after the return of the coroutine function but before
 * yielding back to the parent context. Deferred calls occur in LIFO order.
 *
 * The block will be copied upon creation, and it will be released
 * after exiting.
 *
 * @param  block  block to execute
 */
extern int
strand_defer_b (void (^block)(void));

#endif

#endif

