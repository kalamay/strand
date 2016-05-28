#include "defer.h"

#include <stdlib.h>
#include <assert.h>
#ifdef __BLOCKS__
#include <Block.h>
#endif

struct StrandDefer {
	StrandDefer *next;
	void (*fn) (void *);
	void *data;
};

static __thread StrandDefer *pool = NULL;

int
strand_defer_add (StrandDefer **defp, void (*fn) (void *), void *data)
{
	assert (defp != NULL);
	assert (fn != NULL);

	StrandDefer *def = pool;
	if (def != NULL) {
		pool = def->next;
	}
	else {
		def = malloc (sizeof (*def));
		if (def == NULL) return -1;
	}

	def->next = *defp;
	def->fn = fn;
	def->data = data;

	(*defp) = def;

	return 0;
}

void
strand_defer_run (StrandDefer **defp)
{
	assert (defp != NULL);

	StrandDefer *def = *defp;
	*defp = NULL;

	while (def) {
		StrandDefer *next = def->next;
		def->fn (def->data);
		def->next = pool;
		pool = def;
		def = next;
	}
}

#ifdef __BLOCKS__

static void
defer_shim (void *data)
{
	void (^block)(void) = data;
	block ();
	Block_release (block);
}

int
strand_defer_add_b (StrandDefer **d, void (^block)(void))
{
	return strand_defer_add (d, defer_shim, Block_copy (block));
}

#endif

