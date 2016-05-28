#if defined(__APPLE__)
# define sym(n) _##n
#else
# define sym(n) n
#endif

#if defined(__x86_64__)
# if defined(__linux__)
#  define align p2align 4,,5
# elif defined(BSD)
#  define align align 16, 0x90
# elif defined(__APPLE__)
#  define align align 4, 0x90
# endif
# include "context/amd64.s"
#endif

#define globl(n) globl sym(n)

.text
.globl(strand_swap)
.align
sym(strand_swap): /* strand_swap (save, load) */
	context_save
	context_load

.text
.globl(strand_save)
.align
sym(strand_save): /* strand_save (ctx) */
	context_save
	ret

.text
.globl(strand_load)
.align
sym(strand_load): /* strand_load (ctx) */
	context_load

