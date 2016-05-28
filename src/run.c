#include <stdio.h>
#include <inttypes.h>

#include "strand.h"

int
main (void)
{
	Strand *s;

	s = strand_new_b (^(uintptr_t val) {
		uintptr_t n;
		for (n = 1; n < 10; n++) {
			val = strand_yield (n * n);
		}
		return n * n;
	});

	while (strand_alive (s)) {
		uintptr_t val = strand_resume (s, 0);
		printf ("val: %" PRIuPTR "\n", val);
	}

	strand_free (&s);
}

