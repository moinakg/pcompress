#
# The Keccak sponge function, designed by Guido Bertoni, Joan Daemen,
# Michaël Peeters and Gilles Van Assche. For more information, feedback or
# questions, please refer to our website: http://keccak.noekeon.org/
#
# Implementation by Ronny Van Keer,
# hereby denoted as "the implementer".
# 
# To the extent possible under law, the implementer has waived all copyright
# and related or neighboring rights to the source code in this file.
# http://creativecommons.org/publicdomain/zero/1.0/
#

	.text


#// --- defines

.equ UseSIMD, 1


.equ _ba,  0*8
.equ _be,  1*8
.equ _bi,  2*8
.equ _bo,  3*8
.equ _bu,  4*8
.equ _ga,  5*8
.equ _ge,  6*8
.equ _gi,  7*8
.equ _go,  8*8
.equ _gu,  9*8
.equ _ka, 10*8
.equ _ke, 11*8
.equ _ki, 12*8
.equ _ko, 13*8
.equ _ku, 14*8
.equ _ma, 15*8
.equ _me, 16*8
.equ _mi, 17*8
.equ _mo, 18*8
.equ _mu, 19*8
.equ _sa, 20*8
.equ _se, 21*8
.equ _si, 22*8
.equ _so, 23*8
.equ _su, 24*8


# round vars
.equ %r15,		%r15

.macro	mKeccakRound	iState, oState, rc, lastRound

		movq		%rbp, %rbx
		rolq		%rbx

		movq		_bi(\iState), %r12
		xorq		_gi(\iState), %rdx
		xorq		%r15, %rbx
		xorq		_ki(\iState), %r12
		xorq		_mi(\iState), %rdx
		xorq		%rdx, %r12

		movq		%r12, %rcx
		rolq		%rcx

		movq		_bo(\iState), %r13
		xorq		_go(\iState), %r8
		xorq		%rsi, %rcx
		xorq		_ko(\iState), %r13
		xorq		_mo(\iState), %r8
		xorq		%r8, %r13

		movq		%r13, %rdx
		rolq		%rdx

		movq		%r15, %r8
		xorq		%rbp, %rdx
		rolq		%r8

		movq		%rsi, %r9
		xorq		%r12, %r8
		rolq		%r9

		movq		_ba(\iState), %r10
		movq		_ge(\iState), %r11
		xorq		%r13, %r9
		movq		_ki(\iState), %r12
		movq		_mo(\iState), %r13
		movq		_su(\iState), %r14
		xorq		%rcx, %r11
		rolq		$44, %r11
		xorq		%rdx, %r12
		xorq		%rbx, %r10
		rolq		$43, %r12

		movq		%r11, %rsi
		movq		$\rc, %rax
		orq		%r12, %rsi
		xorq		%r10, %rax
		xorq		%rax, %rsi
		movq		%rsi, _ba(\oState)

		xorq		%r9, %r14
		rolq		$14, %r14
		movq		%r10, %r15
		andq		%r11, %r15
		xorq		%r14, %r15
		movq		%r15, _bu(\oState)

		xorq		%r8, %r13
		rolq		$21, %r13
		movq		%r13, %rax
		andq		%r14, %rax
		xorq		%r12, %rax
		movq		%rax, _bi(\oState)

		notq		%r12
		orq		%r10, %r14
		orq		%r13, %r12
		xorq		%r13, %r14
		xorq		%r11, %r12
		movq		%r14, _bo(\oState)
		movq		%r12, _be(\oState)
		.if		\lastRound == 0
		movq		%r12, %rbp
		.endif


		movq		_gu(\iState), %r11
		xorq		%r9, %r11
		movq		_ka(\iState), %r12
		rolq		$20, %r11
		xorq		%rbx, %r12
		rolq		$3,  %r12
		movq		_bo(\iState), %r10
		movq		%r11, %rax
		orq		%r12, %rax
		xorq		%r8, %r10
		movq		_me(\iState), %r13
		movq		_si(\iState), %r14
		rolq		$28, %r10
		xorq		%r10, %rax
		movq		%rax, _ga(\oState)
		.if		\lastRound == 0
		xor 		%rax, %rsi
		.endif

		xorq		%rcx, %r13
		rolq		$45, %r13
		movq		%r12, %rax
		andq		%r13, %rax
		xorq		%r11, %rax
		movq		%rax, _ge(\oState)
		.if		\lastRound == 0
		xorq		%rax, %rbp
		.endif

		xorq		%rdx, %r14
		rolq		$61, %r14
		movq		%r14, %rax
		orq		%r10, %rax
		xorq		%r13, %rax
		movq		%rax, _go(\oState)

		andq		%r11, %r10
		xorq		%r14, %r10
		movq		%r10, _gu(\oState)
		notq		%r14
		.if		\lastRound == 0
		xorq		%r10, %r15
		.endif

		orq		%r14, %r13
		xorq		%r12, %r13
		movq		%r13, _gi(\oState)


		movq		_be(\iState), %r10
		movq		_gi(\iState), %r11
		movq		_ko(\iState), %r12
		movq		_mu(\iState), %r13
		movq		_sa(\iState), %r14
		xorq		%rdx, %r11
		rolq		$6,  %r11
		xorq		%r8, %r12
		rolq		$25, %r12
		movq		%r11, %rax
		orq		%r12, %rax
		xorq		%rcx, %r10
		rolq		$1,  %r10
		xorq		%r10, %rax
		movq		%rax, _ka(\oState)
		.if		\lastRound == 0
		xor 		%rax, %rsi
		.endif

		xorq		%r9, %r13
		rolq		$8,  %r13
		movq		%r12, %rax
		andq		%r13, %rax
		xorq		%r11, %rax
		movq		%rax, _ke(\oState)
		.if		\lastRound == 0
		xorq		%rax, %rbp
		.endif

		xorq		%rbx, %r14
		rolq		$18, %r14
		notq		%r13
		movq		%r13, %rax
		andq		%r14, %rax
		xorq		%r12, %rax
		movq		%rax, _ki(\oState)

		movq		%r14, %rax
		orq		%r10, %rax
		xorq		%r13, %rax
		movq		%rax, _ko(\oState)

		andq		%r11, %r10
		xorq		%r14, %r10
		movq		%r10, _ku(\oState)
		.if		\lastRound == 0
		xorq		%r10, %r15
		.endif

		movq		_ga(\iState), %r11
		xorq		%rbx, %r11
		movq		_ke(\iState), %r12
		rolq		$36, %r11
		xorq		%rcx, %r12
		movq		_bu(\iState), %r10
		rolq		$10, %r12
		movq		%r11, %rax
		movq		_mi(\iState), %r13
		andq		%r12, %rax
		xorq		%r9, %r10
		movq		_so(\iState), %r14
		rolq		$27, %r10
		xorq		%r10, %rax
		movq		%rax, _ma(\oState)
		.if		\lastRound == 0
		xor 		%rax, %rsi
		.endif

		xorq		%rdx, %r13
		rolq		$15, %r13
		movq		%r12, %rax
		orq		%r13, %rax
		xorq		%r11, %rax
		movq		%rax, _me(\oState)
		.if		\lastRound == 0
		xorq		%rax, %rbp
		.endif

		xorq		%r8, %r14
		rolq		$56, %r14
		notq		%r13
		movq		%r13, %rax
		orq		%r14, %rax
		xorq		%r12, %rax
		movq		%rax, _mi(\oState)

		orq		%r10, %r11
		xorq		%r14, %r11
		movq		%r11, _mu(\oState)

		andq		%r10, %r14
		xorq		%r13, %r14
		movq		%r14, _mo(\oState)
		.if		\lastRound == 0
		xorq		%r11, %r15
		.endif


		movq		_bi(\iState), %r10
		movq		_go(\iState), %r11
		movq		_ku(\iState), %r12
		xorq		%rdx, %r10
		movq		_ma(\iState), %r13
		rolq		$62, %r10
		xorq		%r8, %r11
		movq		_se(\iState), %r14
		rolq		$55, %r11

		xorq		%r9, %r12
		movq		%r10, %r9
		xorq		%rcx, %r14
		rolq		$2,  %r14
		andq		%r11, %r9
		xorq		%r14, %r9
		movq		%r9, _su(\oState)

		rolq		$39, %r12
		.if		\lastRound == 0
		xorq		%r9, %r15
		.endif
		notq		%r11
		xorq		%rbx, %r13
		movq		%r11, %rbx
		andq		%r12, %rbx
		xorq		%r10, %rbx
		movq		%rbx, _sa(\oState)
		.if		\lastRound == 0
		xor 		%rbx, %rsi
		.endif

		rolq		$41, %r13
		movq		%r12, %rcx
		orq		%r13, %rcx
		xorq		%r11, %rcx
		movq		%rcx, _se(\oState)
		.if		\lastRound == 0
		xorq		%rcx, %rbp
		.endif

		movq		%r13, %rdx
		movq		%r14, %r8
		andq		%r14, %rdx
		orq		%r10, %r8
		xorq		%r12, %rdx
		xorq		%r13, %r8
		movq		%rdx, _si(\oState)
		movq		%r8, _so(\oState)

		.endm

.macro	mKeccakPermutation	

		subq		$8*25, %rsp

		movq		_ba(%rdi), %rsi             
		movq		_be(%rdi), %rbp
		movq		_bu(%rdi), %r15

		xorq		_ga(%rdi), %rsi             
		xorq		_ge(%rdi), %rbp
		xorq		_gu(%rdi), %r15             

		xorq		_ka(%rdi), %rsi             
		xorq		_ke(%rdi), %rbp
		xorq		_ku(%rdi), %r15             

		xorq		_ma(%rdi), %rsi             
		xorq		_me(%rdi), %rbp
		xorq		_mu(%rdi), %r15             

		xorq		_sa(%rdi), %rsi
		xorq		_se(%rdi), %rbp
		movq		_si(%rdi), %rdx
		movq		_so(%rdi), %r8
		xorq		_su(%rdi), %r15             


		mKeccakRound	%rdi, %rsp, 0x0000000000000001, 0
		mKeccakRound	%rsp, %rdi, 0x0000000000008082, 0
		mKeccakRound	%rdi, %rsp, 0x800000000000808a, 0
		mKeccakRound	%rsp, %rdi, 0x8000000080008000, 0
		mKeccakRound	%rdi, %rsp, 0x000000000000808b, 0
		mKeccakRound	%rsp, %rdi, 0x0000000080000001, 0

		mKeccakRound	%rdi, %rsp, 0x8000000080008081, 0
		mKeccakRound	%rsp, %rdi, 0x8000000000008009, 0
		mKeccakRound	%rdi, %rsp, 0x000000000000008a, 0
		mKeccakRound	%rsp, %rdi, 0x0000000000000088, 0
		mKeccakRound	%rdi, %rsp, 0x0000000080008009, 0
		mKeccakRound	%rsp, %rdi, 0x000000008000000a, 0

		mKeccakRound	%rdi, %rsp, 0x000000008000808b, 0
		mKeccakRound	%rsp, %rdi, 0x800000000000008b, 0
		mKeccakRound	%rdi, %rsp, 0x8000000000008089, 0
		mKeccakRound	%rsp, %rdi, 0x8000000000008003, 0
		mKeccakRound	%rdi, %rsp, 0x8000000000008002, 0
		mKeccakRound	%rsp, %rdi, 0x8000000000000080, 0

		mKeccakRound	%rdi, %rsp, 0x000000000000800a, 0
		mKeccakRound	%rsp, %rdi, 0x800000008000000a, 0
		mKeccakRound	%rdi, %rsp, 0x8000000080008081, 0
		mKeccakRound	%rsp, %rdi, 0x8000000000008080, 0
		mKeccakRound	%rdi, %rsp, 0x0000000080000001, 0
		mKeccakRound	%rsp, %rdi, 0x8000000080008008, 1

		addq		$8*25, %rsp

		.endm

.macro	mPushRegs	

	pushq		%rbx
	pushq		%rbp
	pushq		%r12
	pushq		%r13
	pushq		%r14
	pushq		%r15

	.endm


.macro	mPopRegs	

	popq		%r15
	popq		%r14
	popq		%r13
	popq		%r12
	popq		%rbp
	popq		%rbx

	.endm


.macro	mXorState128	input, state, offset
	.if 		UseSIMD == 0
	movq		\offset(\input), %rax
	movq		\offset+8(\input), %rcx
	xorq		%rax, \offset(\state)
	xorq		%rcx, \offset+8(\state)
	.else
	movdqu		\offset(\input), %xmm0
	pxor		\offset(\state), %xmm0
	movdqu		%xmm0, \offset(\state)
	.endif
	.endm

.macro	mXorState256	input, state, offset
	.if 		UseSIMD == 0
	movq		\offset(\input), %rax
	movq		\offset+8(\input), %r10
	movq		\offset+16(\input), %rcx
	movq		\offset+24(\input), %r8
	xorq		%rax, \offset(\state)
	xorq		%r10, \offset+8(\state)
	xorq		%rcx, \offset+16(\state)
	xorq		%r8,  \offset+24(\state)
	.else
	movdqu		\offset(\input), %xmm0
	pxor		\offset(\state), %xmm0
	movdqu		\offset+16(\input), %xmm1
	pxor		\offset+16(\state), %xmm1
	movdqu		%xmm0, \offset(\state)
	movdqu		%xmm1, \offset+16(\state)
	.endif
	.endm

.macro	mXorState512	input, state, offset
	.if 		UseSIMD == 0
	mXorState256	\input, \state, \offset
	mXorState256	\input, \state, \offset+32
	.else
	movdqu		\offset(\input), %xmm0
	movdqu		\offset+16(\input), %xmm1
	pxor		\offset(\state), %xmm0
	movdqu		\offset+32(\input), %xmm2
	pxor		\offset+16(\state), %xmm1
	movdqu		%xmm0, \offset(\state)
	movdqu		\offset+48(\input), %xmm3
	pxor		\offset+32(\state), %xmm2
	movdqu		%xmm1, \offset+16(\state)
	pxor		\offset+48(\state), %xmm3
	movdqu		%xmm2, \offset+32(\state)
	movdqu		%xmm3, \offset+48(\state)
	.endif
	.endm

# -------------------------------------------------------------------------

	.align	2
	.global	KeccakPermutation, @function
KeccakPermutation:

	mPushRegs
	mKeccakPermutation
	mPopRegs
	ret

# -------------------------------------------------------------------------

	.align	2
	.global	KeccakAbsorb576bits, @function
KeccakAbsorb576bits:

	mXorState512	%rsi, %rdi, 0
	movq		64(%rsi), %rax
	xorq		%rax, 64(%rdi)
	mPushRegs
	mKeccakPermutation
	mPopRegs
	ret

# -------------------------------------------------------------------------

	.align	2
	.global	KeccakAbsorb832bits, @function
KeccakAbsorb832bits:

	mXorState512	%rsi, %rdi, 0
	mXorState256	%rsi, %rdi, 64
	movq		96(%rsi), %rax
	xorq		%rax, 96(%rdi)
	mPushRegs
	mKeccakPermutation
	mPopRegs
	ret

# -------------------------------------------------------------------------

	.align	2
	.global	KeccakAbsorb1024bits, @function
KeccakAbsorb1024bits:

	mXorState512	%rsi, %rdi, 0
	mXorState512	%rsi, %rdi, 64
	mPushRegs
	mKeccakPermutation
	mPopRegs
	ret

# -------------------------------------------------------------------------

	.align	2
	.global	KeccakAbsorb1088bits, @function
KeccakAbsorb1088bits:

	mXorState512	%rsi, %rdi, 0
	mXorState512	%rsi, %rdi, 64
	movq		128(%rsi), %rax
	xorq		%rax, 128(%rdi)
	mPushRegs
	mKeccakPermutation
	mPopRegs
	ret

# -------------------------------------------------------------------------

	.align	2
	.global	KeccakAbsorb1152bits, @function
KeccakAbsorb1152bits:

	mXorState512	%rsi, %rdi, 0
	mXorState512	%rsi, %rdi, 64
	mXorState128	%rsi, %rdi, 128
	mPushRegs
	mKeccakPermutation
	mPopRegs
	ret

# -------------------------------------------------------------------------

	.align	2
	.global	KeccakAbsorb1344bits, @function
KeccakAbsorb1344bits:

	mXorState512	%rsi, %rdi, 0
	mXorState512	%rsi, %rdi, 64
	mXorState256	%rsi, %rdi, 128
	movq		160(%rsi), %rax
	xorq		%rax, 160(%rdi)
	mPushRegs
	mKeccakPermutation
	mPopRegs
	ret

# -------------------------------------------------------------------------

	.align	2
	.global	KeccakAbsorb, @function
KeccakAbsorb:

	movq		%rdi, %r9

	test		$16, %rdx
	jz		xorInputToState8
	mXorState512	%rsi, %r9, 0
	mXorState512	%rsi, %r9, 64
	addq		$128, %rsi
	addq		$128, %r9

xorInputToState8:
	test		$8, %rdx
	jz		xorInputToState4
	mXorState512	%rsi, %r9, 0
	addq		$64, %rsi
	addq		$64, %r9

xorInputToState4:
	test		$4, %rdx
	jz		xorInputToState2
	mXorState256	%rsi, %r9, 0
	addq		$32, %rsi
	addq		$32, %r9

xorInputToState2:
	test		$2, %rdx
	jz		xorInputToState1
	mXorState128	%rsi, %r9, 0
	addq		$16, %rsi
	addq		$16, %r9

xorInputToState1:
	test		$1, %rdx
	jz		xorInputToStateDone
	movq		(%rsi), %rax
	xorq		%rax, (%r9)

xorInputToStateDone:

	mPushRegs
	mKeccakPermutation
	mPopRegs
	ret

# -------------------------------------------------------------------------

	.align	2
	.global	KeccakInitializeState, @function
KeccakInitializeState:
	xorq		%rax, %rax
	xorq		%rcx, %rcx
	notq		%rcx

	.if 		UseSIMD == 0
	movq		%rax,   0*8(%rdi)
	movq		%rcx,   1*8(%rdi)
	movq		%rcx,   2*8(%rdi)
	movq		%rax,   3*8(%rdi)
	movq		%rax,   4*8(%rdi)
	movq		%rax,   5*8(%rdi)
	movq		%rax,   6*8(%rdi)
	movq		%rax,   7*8(%rdi)
	movq		%rcx,   8*8(%rdi)
	movq		%rax,   9*8(%rdi)
	movq		%rax,  10*8(%rdi)
	movq		%rax,  11*8(%rdi)
	movq		%rcx,  12*8(%rdi)
	movq		%rax,  13*8(%rdi)
	movq		%rax,  14*8(%rdi)
	movq		%rax,  15*8(%rdi)
	movq		%rax,  16*8(%rdi)
	movq		%rcx,  17*8(%rdi)
	movq		%rax,  18*8(%rdi)
	movq		%rax,  19*8(%rdi)
	movq		%rcx,  20*8(%rdi)
	movq		%rax,  21*8(%rdi)
	movq		%rax,  22*8(%rdi)
	movq		%rax,  23*8(%rdi)
	movq		%rax,  24*8(%rdi)
	.else
	pxor		%xmm0, %xmm0

	movq		%rax,   0*8(%rdi)
	movq		%rcx,   1*8(%rdi)
	movq		%rcx,   2*8(%rdi)
	movq		%rax,   3*8(%rdi)
	movdqu		%xmm0,  4*8(%rdi)
	movdqu		%xmm0,  6*8(%rdi)
	movq		%rcx,   8*8(%rdi)
	movq		%rax,   9*8(%rdi)
	movdqu		%xmm0, 10*8(%rdi)
	movq		%rcx,  12*8(%rdi)
	movq		%rax,  13*8(%rdi)
	movdqu		%xmm0, 14*8(%rdi)
	movq		%rax,  16*8(%rdi)
	movq		%rcx,  17*8(%rdi)
	movdqu		%xmm0, 18*8(%rdi)
	movq		%rcx,  20*8(%rdi)
	movq		%rax,  21*8(%rdi)
	movdqu		%xmm0, 22*8(%rdi)
	movq		%rax,  24*8(%rdi)
	.endif
	ret

# -------------------------------------------------------------------------

	.align	2
	.global	KeccakExtract1024bits, @function
KeccakExtract1024bits:

	movq		0*8(%rdi), %rax
	movq		1*8(%rdi), %rcx
	movq		2*8(%rdi), %rdx
	movq		3*8(%rdi), %r8
	notq		%rcx
	notq		%rdx
	movq		%rax, 0*8(%rsi)
	movq		%rcx, 1*8(%rsi)
	movq		%rdx, 2*8(%rsi)
	movq		%r8,  3*8(%rsi)

	movq		4*8(%rdi), %rax
	movq		5*8(%rdi), %rcx
	movq		6*8(%rdi), %rdx
	movq		7*8(%rdi), %r8
	movq		%rax, 4*8(%rsi)
	movq		%rcx, 5*8(%rsi)
	movq		%rdx, 6*8(%rsi)
	movq		%r8,  7*8(%rsi)

	movq		 8*8(%rdi), %rax
	movq		 9*8(%rdi), %rcx
	movq		10*8(%rdi), %rdx
	movq		11*8(%rdi), %r8
	notq		%rax
	movq		%rax,  8*8(%rsi)
	movq		%rcx,  9*8(%rsi)
	movq		%rdx, 10*8(%rsi)
	movq		%r8,  11*8(%rsi)

	movq		12*8(%rdi), %rax
	movq		13*8(%rdi), %rcx
	movq		14*8(%rdi), %rdx
	movq		15*8(%rdi), %r8
	notq		%rax
	movq		%rax, 12*8(%rsi)
	movq		%rcx, 13*8(%rsi)
	movq		%rdx, 14*8(%rsi)
	movq		%r8,  15*8(%rsi)
	ret

