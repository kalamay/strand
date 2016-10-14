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

static uintptr_t *
stack_start (void *stack, size_t len)
{
	uintptr_t *s = (uintptr_t *)((uint8_t *)stack + len);
	s = (void *)((uintptr_t)s - (uintptr_t)s%16);
	return s - 1;
}

void
strand_ctx_init (uintptr_t *ctx, void *stack, size_t len,
		uintptr_t ip, uintptr_t a1, uintptr_t a2)
{
	uintptr_t *s = stack_start (stack, len);
	*s = 0;
	ctx[RDI] = a1;
	ctx[RSI] = a2;
	ctx[RIP] = ip;
	ctx[RSP] = (uintptr_t)s;
}

size_t
strand_ctx_stack_size (const uintptr_t *ctx, void *stack, size_t len, bool current)
{
	register uintptr_t *s = stack_start (stack, len);
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
		"\trbx: " FMTxREG "\n"
		"\trbp: " FMTxREG "\n"
		"\tr12: " FMTxREG "\n"
		"\tr13: " FMTxREG "\n"
		"\tr14: " FMTxREG "\n"
		"\tr15: " FMTxREG "\n"
		"\trdi: " FMTxREG "\n"
		"\trsi: " FMTxREG "\n"
		"\trip: " FMTxREG "\n"
		"\trsp: " FMTxREG "\n",
		ctx[RBX], ctx[RBP], ctx[R12], ctx[R13], ctx[R14],
		ctx[R15], ctx[RDI], ctx[RSI], ctx[RIP], ctx[RSP]);
}

__asm__ (
	".text\n"
#if defined (__APPLE__)
	".globl _strand_ctx_swap\n"
	"_strand_ctx_swap:\n"
#else
	".globl strand_ctx_swap\n"
	"strand_ctx_swap:\n\t"
#endif
		"movq      %rbx,    0(%rdi) \r\n"
		"movq      %rbp,    8(%rdi) \r\n"
		"movq      %r12,   16(%rdi) \r\n"
		"movq      %r13,   24(%rdi) \r\n"
		"movq      %r14,   32(%rdi) \r\n"
		"movq      %r15,   40(%rdi) \r\n"
		"movq      %rdi,   48(%rdi) \r\n"
		"movq      %rsi,   56(%rdi) \r\n"
		"movq     (%rsp),     %rcx  \r\n" // %rip
		"movq      %rcx,   64(%rdi) \r\n"
		"leaq    8(%rsp),     %rcx  \r\n" // %rsp
		"movq      %rcx,   72(%rdi) \r\n"
		"movq   72(%rsi),     %rsp  \r\n"
		"movq    0(%rsi),     %rbx  \r\n"
		"movq    8(%rsi),     %rbp  \r\n"
		"movq   16(%rsi),     %r12  \r\n"
		"movq   24(%rsi),     %r13  \r\n"
		"movq   32(%rsi),     %r14  \r\n"
		"movq   40(%rsi),     %r15  \r\n"
		"movq   48(%rsi),     %rdi  \r\n"
		"movq   64(%rsi),     %rcx  \r\n"
		"movq   56(%rsi),     %rsi  \r\n"
		"jmp      *%rcx             \r\n"
);

