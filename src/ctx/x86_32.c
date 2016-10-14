#define STRAND_CTX_REG_COUNT 7

#define EBX 0
#define ESI 1
#define EDI 2
#define EBP 3
#define EIP 4
#define ESP 5
#define ECX 6

static uintptr_t *
stack_start (uint8_t *stack, size_t len)
{
	uintptr_t *s = (uintptr_t *)(stack + len - sizeof (uintptr_t)*2);
	s = (void *)((uintptr_t)s - (uintptr_t)s%16);
	return s - 1;
}

void
strand_ctx_init (uintptr_t *ctx, void *stack, size_t len,
		uintptr_t ip, uintptr_t a1, uintptr_t a2)
{
	uintptr_t *s = stack_start (stack, len);
	*s = 0;

	s[1] = a1;
	s[2] = a2;
	ctx[EIP] = ip;
	ctx[ESP] = (uintptr_t)s;
}

size_t
strand_ctx_stack_size (const uintptr_t *ctx, void *stack, size_t len, bool current)
{
	register uintptr_t *s = stack_start (stack, len);
	register uintptr_t sp;
	if (current) {
		__asm__ ("movl %%esp, %0" : "=r" (sp));
	}
	else {
		sp = ctx[ESP];
	}
	return (uintptr_t)s - sp;
}

void
strand_ctx_print (const uintptr_t *ctx, FILE *out)
{
	fprintf (out,
		"\tebx: 0x%08" PRIxPTR "\n"
		"\tesi: 0x%08" PRIxPTR "\n"
		"\tedi: 0x%08" PRIxPTR "\n"
		"\tebp: 0x%08" PRIxPTR "\n"
		"\teip: 0x%08" PRIxPTR "\n"
		"\tesp: 0x%08" PRIxPTR "\n"
		"\tecx: 0x%08" PRIxPTR "\n",
		ctx[EBX], ctx[ESI], ctx[EDI], ctx[EBP], ctx[EIP], ctx[ESP], ctx[ECX]);
}

__asm__ (
	".text\n"
#if defined (__APPLE__)
	"_strand_ctx_swap:\n"
#else
	"strand_ctx_swap:\n\t"
#endif
		"movl    4(%esp),     %eax  \n\t"
		"movl      %ecx,   24(%eax) \n\t"
		"movl      %ebx,    0(%eax) \n\t"
		"movl      %esi,    4(%eax) \n\t"
		"movl      %edi,    8(%eax) \n\t"
		"movl      %ebp,   12(%eax) \n\t"
		"movl     (%esp),     %ecx  \n\t"
		"movl      %ecx,   16(%eax) \n\t"
		"leal    4(%esp),     %ecx  \n\t"
		"movl      %ecx,   20(%eax) \n\t"
		"movl    8(%esp),     %eax  \n\t"
		"movl   16(%eax),     %ecx  \n\t"
		"movl   20(%eax),     %esp  \n\t"
		"pushl     %ecx             \n\t"
		"movl    0(%eax),     %ebx  \n\t"
		"movl    4(%eax),     %esi  \n\t"
		"movl    8(%eax),     %edi  \n\t"
		"movl   12(%eax),     %ebp  \n\t"
		"movl   24(%eax),     %ecx  \n\t"
		"ret                        \n\t"
);

