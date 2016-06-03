#include <stdio.h>
#include <ctype.h>
#include <inttypes.h>
#include <string.h>
#include "strand.h"

typedef struct {
	const char *start;
	size_t len;
} Slice;

int
main (void)
{
	char data[] = "this is a test with a few words";

	Strand *t1 = strand_new_b (^(uintptr_t val) {
		char *buf = (char *)val;
		Slice s = { buf, 0 };
		for (; *buf; buf++) {
			if (isspace (*buf)) {
				s.len = buf - s.start;
				strand_yield ((uintptr_t)&s);
				s.start = buf+1;
			}
		}
		s.len = strlen (s.start);
		return (uintptr_t)&s;
	});

	Strand *t2 = strand_new_b (^(uintptr_t val) {
		char *test = strand_malloc (256);
		while (strand_alive (t1)) {
			Slice *word = (Slice *)strand_resume (t1, val);
			if (word->len > 2) {
				memcpy (test, word->start, word->len);
				test[word->len] = '\0';
				strand_yield ((uintptr_t)word);
			}
		}
		return (uintptr_t)NULL;
	});

	while (strand_alive (t2)) {
		Slice *word = (Slice *)strand_resume (t2, (uintptr_t)data);
		if (word != NULL) {
			printf ("word: %.*s\n", (int)word->len, word->start);
		}
	}

	strand_free (&t1);
	strand_free (&t2);
}

