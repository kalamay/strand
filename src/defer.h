#ifndef STRAND_DEFER_H
#define STRAND_DEFER_H

#include <siphon/common.h>

typedef struct StrandDefer StrandDefer;

/**
 * Append a deferred function to the list
 *
 * When run, these functions will be run in LIFO order.
 * Initially, `*d` should be set to `NULL`.
 *
 * @param  d     reference to head of deferred list
 * @param  fn    function for later execution
 * @param  data  data to pass to `fn`
 * @return  `0` on success `-errno` on failure
 */
SP_EXPORT int
strand_defer_add (StrandDefer **d, void (*fn) (void *), void *data);

/**
 * Runs and clears the deferred list
 *
 * The deferred functions will be executed in LIFO order.
 *
 * @param  d  reference to head of deferred list
 */
SP_EXPORT void
strand_defer_run (StrandDefer **d);

/**
 * Creates an automatic allocation with a deferred free
 *
 * @param  d     reference to head of deferred list
 * @param  size  number of bytes to allocate
 * @return  pointer to allocation or `NULL`
 */
SP_EXPORT void *
strand_defer_malloc (StrandDefer **d, size_t size);

/**
 * Creates a zeroed automatic allocation with a deferred free
 *
 * @param  d      reference to head of deferred list
 * @param  count  number of contiguous objects
 * @param  size   number of bytes for each object
 * @return  pointer to allocation or `NULL`
 */
SP_EXPORT void *
strand_defer_calloc (StrandDefer **d, size_t count, size_t size);

#ifdef __BLOCKS__

/**
 * Append a deferred block to the list
 *
 * The `block` will be copied. After invoked, it will be released.
 * Initially, `*d` should be set to `NULL`.
 *
 * @param  d      reference to head of deferred list
 * @param  block  blok for later execution
 * @return  `0` on success `-errno` on failure
 */
SP_EXPORT int
strand_defer_add_b (StrandDefer **d, void (^block)(void));

#endif

#endif

