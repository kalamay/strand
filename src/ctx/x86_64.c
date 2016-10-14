#define STRAND_CTX_REG_COUNT 10

#define RBX 0
#define RBP 1
#define R12 2
#define R13 3
#define R14 4
#define R15 5
#define RDI 6
#define RSI 7
#define RIP 8
#define RSP 9

/**
 * Gets a pointer to the starting address of the stack
 *
 * @param  stack  lowest address of the stack
 * @param  len    number of bytes for the stack
 * @return  pointer to th starting address
 */
static uintptr_t *
strand_stack_start (uint8_t *stack, size_t len)
{
	uintptr_t *s = (uintptr_t *)(stack + len);
	s = (void *)((uintptr_t)s - (uintptr_t)s%16);
	return s - 1;
}

void
strand_ctx_init (uintptr_t *ctx, void *stack, size_t len,
		uintptr_t ip, uintptr_t a1, uintptr_t a2)
{
	uintptr_t *s = strand_stack_start (stack, len);
	*s = 0;

	ctx[RDI] = a1;
	ctx[RSI] = a2;
	ctx[RIP] = ip;
	ctx[RSP] = (uintptr_t)s;
}

size_t
strand_ctx_stack_size (const uintptr_t *ctx, void *stack, size_t len, bool current)
{
	register uintptr_t *s = strand_stack_start (stack, len);
	register uintptr_t sp;
	if (current) {
		__asm__ ("movq %%rsp, %0" : "=r" (sp));
	}
	else {
		sp = ctx[RSP];
	}
	return (uintptr_t)s - sp;
}

void
strand_ctx_print (const uintptr_t *ctx, FILE *out)
{
	fprintf (out,
		"\trbx: 0x%016" PRIxPTR "\n"
		"\trbp: 0x%016" PRIxPTR "\n"
		"\tr12: 0x%016" PRIxPTR "\n"
		"\tr13: 0x%016" PRIxPTR "\n"
		"\tr14: 0x%016" PRIxPTR "\n"
		"\tr15: 0x%016" PRIxPTR "\n"
		"\trdi: 0x%016" PRIxPTR "\n"
		"\trsi: 0x%016" PRIxPTR "\n"
		"\trip: 0x%016" PRIxPTR "\n"
		"\trsp: 0x%016" PRIxPTR "\n",
		ctx[RBX], ctx[RBP], ctx[R12], ctx[R13], ctx[R14],
		ctx[R15], ctx[RDI], ctx[RSI], ctx[RIP], ctx[RSP]);
}

__asm__ (
	".text                          \n"
#if defined (__APPLE__)
	"_strand_ctx_swap:              \n\t"
#else
	"strand_ctx_swap:               \n\t"
#endif
		"movq      %rbx,    0(%rdi) \n\t"
		"movq      %rbp,    8(%rdi) \n\t"
		"movq      %r12,   16(%rdi) \n\t"
		"movq      %r13,   24(%rdi) \n\t"
		"movq      %r14,   32(%rdi) \n\t"
		"movq      %r15,   40(%rdi) \n\t"
		"movq      %rdi,   48(%rdi) \n\t"
		"movq      %rsi,   56(%rdi) \n\t"
		"movq     (%rsp),     %rcx  \n\t"
		"movq      %rcx,   64(%rdi) \n\t"
		"leaq    8(%rsp),     %rcx  \n\t"
		"movq      %rcx,   72(%rdi) \n\t"
		"movq   72(%rsi),     %rsp  \n\t"
		"movq    0(%rsi),     %rbx  \n\t"
		"movq    8(%rsi),     %rbp  \n\t"
		"movq   16(%rsi),     %r12  \n\t"
		"movq   24(%rsi),     %r13  \n\t"
		"movq   32(%rsi),     %r14  \n\t"
		"movq   40(%rsi),     %r15  \n\t"
		"movq   48(%rsi),     %rdi  \n\t"
		"movq   64(%rsi),     %rcx  \n\t"
		"movq   56(%rsi),     %rsi  \n\t"
		"jmp      *%rcx             \n\t"
);

