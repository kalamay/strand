.macro context_save
	movq      %rbx,    0(%rdi)
	movq      %rbp,    8(%rdi)
	movq      %r12,   16(%rdi)
	movq      %r13,   24(%rdi)
	movq      %r14,   32(%rdi)
	movq      %r15,   40(%rdi)
	movq      %rdi,   48(%rdi)
	movq      %rsi,   56(%rdi)
	movq     (%rsp),     %rcx
	movq      %rcx,   64(%rdi)
	leaq    8(%rsp),     %rcx
	movq      %rcx,   72(%rdi)
.endm

.macro context_load
	movq    0(%rsi),     %rbx
	movq    8(%rsi),     %rbp
	movq   16(%rsi),     %r12
	movq   24(%rsi),     %r13
	movq   32(%rsi),     %r14
	movq   40(%rsi),     %r15
	movq   48(%rsi),     %rdi
	movq   64(%rsi),     %rcx
	movq   72(%rsi),     %rsp
	movq   56(%rsi),     %rsi
	jmp      *%rcx
.endm
