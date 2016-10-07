#include "mu.h"

#include "../src/strand.h"

static uintptr_t
fib (void *data, uintptr_t val)
{
	(void)data;
	(void)val;
	uintptr_t a = 0, b = 1;
	while (true) {
		uintptr_t r = a;
		a = b;
		b += r;
		strand_yield (r);
	}
	return 0;
}

static uintptr_t
fib3 (void *data, uintptr_t val)
{
	(void)val;
	while (true) {
		strand_resume (data, 0);
		strand_resume (data, 0);
		strand_yield (strand_resume (data, 0));
	}
	return 0;
}

static void
test_fibonacci (void)
{
	static const uintptr_t expect[10] = {
		1,
		5,
		21,
		89,
		377,
		1597,
		6765,
		28657,
		121393,
		514229
	};

	Strand *s1, *s2;
	uintptr_t got[10];

	s1 = strand_new (fib, NULL);
	s2 = strand_new (fib3, s1);

	for (uintptr_t n = 0; n < 10; n++) {
		uintptr_t val = strand_resume (s2, n);
		got[n] = val;
	}

	strand_free (&s1);
	strand_free (&s2);

	mu_assert_uint_eq (expect[0], got[0]);
	mu_assert_uint_eq (expect[1], got[1]);
	mu_assert_uint_eq (expect[2], got[2]);
	mu_assert_uint_eq (expect[3], got[3]);
	mu_assert_uint_eq (expect[4], got[4]);
	mu_assert_uint_eq (expect[5], got[5]);
	mu_assert_uint_eq (expect[6], got[6]);
	mu_assert_uint_eq (expect[7], got[7]);
	mu_assert_uint_eq (expect[8], got[8]);
	mu_assert_uint_eq (expect[9], got[9]);
}

int
main (void)
{
	mu_init ("strand");

	test_fibonacci ();

	mu_exit ();
}

