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


#	arguments
.equ apState,		%rdi
.equ apInput,		%rsi
.equ aNbrWords,		%rdx

#	xor input into state section
.equ xpState,		%r9

# round vars
.equ rT1,		%rax
.equ rpState,		%rdi
.equ rpStack,		%rsp

.equ rDa,		%rbx
.equ rDe,		%rcx
.equ rDi,		%rdx
.equ rDo,		%r8
.equ rDu,		%r9

.equ rBa,		%r10 
.equ rBe,		%r11
.equ rBi,		%r12
.equ rBo,		%r13
.equ rBu,		%r14

.equ rCa,		%rsi
.equ rCe,		%rbp
.equ rCi,		rBi
.equ rCo,		rBo
.equ rCu,		%r15

.macro	mKeccakRound	iState, oState, rc, lastRound

		movq		rCe, rDa
		rolq		rDa

		movq		_bi(\iState), rCi
		xorq		_gi(\iState), rDi
		xorq		rCu, rDa
		xorq		_ki(\iState), rCi
		xorq		_mi(\iState), rDi
		xorq		rDi, rCi

		movq		rCi, rDe
		rolq		rDe

		movq		_bo(\iState), rCo
		xorq		_go(\iState), rDo
		xorq		rCa, rDe
		xorq		_ko(\iState), rCo
		xorq		_mo(\iState), rDo
		xorq		rDo, rCo

		movq		rCo, rDi
		rolq		rDi

		movq		rCu, rDo
		xorq		rCe, rDi
		rolq		rDo

		movq		rCa, rDu
		xorq		rCi, rDo
		rolq		rDu

		movq		_ba(\iState), rBa
		movq		_ge(\iState), rBe
		xorq		rCo, rDu
		movq		_ki(\iState), rBi
		movq		_mo(\iState), rBo
		movq		_su(\iState), rBu
		xorq		rDe, rBe
		rolq		$44, rBe
		xorq		rDi, rBi
		xorq		rDa, rBa
		rolq		$43, rBi

		movq		rBe, rCa
		movq		$\rc, rT1
		orq		rBi, rCa
		xorq		rBa, rT1
		xorq		rT1, rCa
		movq		rCa, _ba(\oState)

		xorq		rDu, rBu
		rolq		$14, rBu
		movq		rBa, rCu
		andq		rBe, rCu
		xorq		rBu, rCu
		movq		rCu, _bu(\oState)

		xorq		rDo, rBo
		rolq		$21, rBo
		movq		rBo, rT1
		andq		rBu, rT1
		xorq		rBi, rT1
		movq		rT1, _bi(\oState)

		notq		rBi
		orq		rBa, rBu
		orq		rBo, rBi
		xorq		rBo, rBu
		xorq		rBe, rBi
		movq		rBu, _bo(\oState)
		movq		rBi, _be(\oState)
		.if		\lastRound == 0
		movq		rBi, rCe
		.endif


		movq		_gu(\iState), rBe
		xorq		rDu, rBe
		movq		_ka(\iState), rBi
		rolq		$20, rBe
		xorq		rDa, rBi
		rolq		$3,  rBi
		movq		_bo(\iState), rBa
		movq		rBe, rT1
		orq		rBi, rT1
		xorq		rDo, rBa
		movq		_me(\iState), rBo
		movq		_si(\iState), rBu
		rolq		$28, rBa
		xorq		rBa, rT1
		movq		rT1, _ga(\oState)
		.if		\lastRound == 0
		xor 		rT1, rCa
		.endif

		xorq		rDe, rBo
		rolq		$45, rBo
		movq		rBi, rT1
		andq		rBo, rT1
		xorq		rBe, rT1
		movq		rT1, _ge(\oState)
		.if		\lastRound == 0
		xorq		rT1, rCe
		.endif

		xorq		rDi, rBu
		rolq		$61, rBu
		movq		rBu, rT1
		orq		rBa, rT1
		xorq		rBo, rT1
		movq		rT1, _go(\oState)

		andq		rBe, rBa
		xorq		rBu, rBa
		movq		rBa, _gu(\oState)
		notq		rBu
		.if		\lastRound == 0
		xorq		rBa, rCu
		.endif

		orq		rBu, rBo
		xorq		rBi, rBo
		movq		rBo, _gi(\oState)


		movq		_be(\iState), rBa
		movq		_gi(\iState), rBe
		movq		_ko(\iState), rBi
		movq		_mu(\iState), rBo
		movq		_sa(\iState), rBu
		xorq		rDi, rBe
		rolq		$6,  rBe
		xorq		rDo, rBi
		rolq		$25, rBi
		movq		rBe, rT1
		orq		rBi, rT1
		xorq		rDe, rBa
		rolq		$1,  rBa
		xorq		rBa, rT1
		movq		rT1, _ka(\oState)
		.if		\lastRound == 0
		xor 		rT1, rCa
		.endif

		xorq		rDu, rBo
		rolq		$8,  rBo
		movq		rBi, rT1
		andq		rBo, rT1
		xorq		rBe, rT1
		movq		rT1, _ke(\oState)
		.if		\lastRound == 0
		xorq		rT1, rCe
		.endif

		xorq		rDa, rBu
		rolq		$18, rBu
		notq		rBo
		movq		rBo, rT1
		andq		rBu, rT1
		xorq		rBi, rT1
		movq		rT1, _ki(\oState)

		movq		rBu, rT1
		orq		rBa, rT1
		xorq		rBo, rT1
		movq		rT1, _ko(\oState)

		andq		rBe, rBa
		xorq		rBu, rBa
		movq		rBa, _ku(\oState)
		.if		\lastRound == 0
		xorq		rBa, rCu
		.endif

		movq		_ga(\iState), rBe
		xorq		rDa, rBe
		movq		_ke(\iState), rBi
		rolq		$36, rBe
		xorq		rDe, rBi
		movq		_bu(\iState), rBa
		rolq		$10, rBi
		movq		rBe, rT1
		movq		_mi(\iState), rBo
		andq		rBi, rT1
		xorq		rDu, rBa
		movq		_so(\iState), rBu
		rolq		$27, rBa
		xorq		rBa, rT1
		movq		rT1, _ma(\oState)
		.if		\lastRound == 0
		xor 		rT1, rCa
		.endif

		xorq		rDi, rBo
		rolq		$15, rBo
		movq		rBi, rT1
		orq		rBo, rT1
		xorq		rBe, rT1
		movq		rT1, _me(\oState)
		.if		\lastRound == 0
		xorq		rT1, rCe
		.endif

		xorq		rDo, rBu
		rolq		$56, rBu
		notq		rBo
		movq		rBo, rT1
		orq		rBu, rT1
		xorq		rBi, rT1
		movq		rT1, _mi(\oState)

		orq		rBa, rBe
		xorq		rBu, rBe
		movq		rBe, _mu(\oState)

		andq		rBa, rBu
		xorq		rBo, rBu
		movq		rBu, _mo(\oState)
		.if		\lastRound == 0
		xorq		rBe, rCu
		.endif


		movq		_bi(\iState), rBa
		movq		_go(\iState), rBe
		movq		_ku(\iState), rBi
		xorq		rDi, rBa
		movq		_ma(\iState), rBo
		rolq		$62, rBa
		xorq		rDo, rBe
		movq		_se(\iState), rBu
		rolq		$55, rBe

		xorq		rDu, rBi
		movq		rBa, rDu
		xorq		rDe, rBu
		rolq		$2,  rBu
		andq		rBe, rDu
		xorq		rBu, rDu
		movq		rDu, _su(\oState)

		rolq		$39, rBi
		.if		\lastRound == 0
		xorq		rDu, rCu
		.endif
		notq		rBe
		xorq		rDa, rBo
		movq		rBe, rDa
		andq		rBi, rDa
		xorq		rBa, rDa
		movq		rDa, _sa(\oState)
		.if		\lastRound == 0
		xor 		rDa, rCa
		.endif

		rolq		$41, rBo
		movq		rBi, rDe
		orq		rBo, rDe
		xorq		rBe, rDe
		movq		rDe, _se(\oState)
		.if		\lastRound == 0
		xorq		rDe, rCe
		.endif

		movq		rBo, rDi
		movq		rBu, rDo
		andq		rBu, rDi
		orq		rBa, rDo
		xorq		rBi, rDi
		xorq		rBo, rDo
		movq		rDi, _si(\oState)
		movq		rDo, _so(\oState)

		.endm

.macro	mKeccakPermutation	

		subq		$8*25, %rsp

		movq		_ba(rpState), rCa             
		movq		_be(rpState), rCe
		movq		_bu(rpState), rCu

		xorq		_ga(rpState), rCa             
		xorq		_ge(rpState), rCe
		xorq		_gu(rpState), rCu             

		xorq		_ka(rpState), rCa             
		xorq		_ke(rpState), rCe
		xorq		_ku(rpState), rCu             

		xorq		_ma(rpState), rCa             
		xorq		_me(rpState), rCe
		xorq		_mu(rpState), rCu             

		xorq		_sa(rpState), rCa
		xorq		_se(rpState), rCe
		movq		_si(rpState), rDi
		movq		_so(rpState), rDo
		xorq		_su(rpState), rCu             


		mKeccakRound	rpState, rpStack, 0x0000000000000001, 0
		mKeccakRound	rpStack, rpState, 0x0000000000008082, 0
		mKeccakRound	rpState, rpStack, 0x800000000000808a, 0
		mKeccakRound	rpStack, rpState, 0x8000000080008000, 0
		mKeccakRound	rpState, rpStack, 0x000000000000808b, 0
		mKeccakRound	rpStack, rpState, 0x0000000080000001, 0

		mKeccakRound	rpState, rpStack, 0x8000000080008081, 0
		mKeccakRound	rpStack, rpState, 0x8000000000008009, 0
		mKeccakRound	rpState, rpStack, 0x000000000000008a, 0
		mKeccakRound	rpStack, rpState, 0x0000000000000088, 0
		mKeccakRound	rpState, rpStack, 0x0000000080008009, 0
		mKeccakRound	rpStack, rpState, 0x000000008000000a, 0

		mKeccakRound	rpState, rpStack, 0x000000008000808b, 0
		mKeccakRound	rpStack, rpState, 0x800000000000008b, 0
		mKeccakRound	rpState, rpStack, 0x8000000000008089, 0
		mKeccakRound	rpStack, rpState, 0x8000000000008003, 0
		mKeccakRound	rpState, rpStack, 0x8000000000008002, 0
		mKeccakRound	rpStack, rpState, 0x8000000000000080, 0

		mKeccakRound	rpState, rpStack, 0x000000000000800a, 0
		mKeccakRound	rpStack, rpState, 0x800000008000000a, 0
		mKeccakRound	rpState, rpStack, 0x8000000080008081, 0
		mKeccakRound	rpStack, rpState, 0x8000000000008080, 0
		mKeccakRound	rpState, rpStack, 0x0000000080000001, 0
		mKeccakRound	rpStack, rpState, 0x8000000080008008, 1

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

	.size	KeccakPermutation, .-KeccakPermutation
	.align	2
	.global	KeccakPermutation
	.type	KeccakPermutation, %function
KeccakPermutation:

	mPushRegs
	mKeccakPermutation
	mPopRegs
	ret

# -------------------------------------------------------------------------

	.size	KeccakAbsorb576bits, .-KeccakAbsorb576bits
	.align	2
	.global	KeccakAbsorb576bits
	.type	KeccakAbsorb576bits, %function
KeccakAbsorb576bits:

	mXorState512	apInput, apState, 0
	movq		64(apInput), %rax
	xorq		%rax, 64(apState)
	mPushRegs
	mKeccakPermutation
	mPopRegs
	ret

# -------------------------------------------------------------------------

	.size	KeccakAbsorb832bits, .-KeccakAbsorb832bits
	.align	2
	.global	KeccakAbsorb832bits
	.type	KeccakAbsorb832bits, %function
KeccakAbsorb832bits:

	mXorState512	apInput, apState, 0
	mXorState256	apInput, apState, 64
	movq		96(apInput), %rax
	xorq		%rax, 96(apState)
	mPushRegs
	mKeccakPermutation
	mPopRegs
	ret

# -------------------------------------------------------------------------

	.size	KeccakAbsorb1024bits, .-KeccakAbsorb1024bits
	.align	2
	.global	KeccakAbsorb1024bits
	.type	KeccakAbsorb1024bits, %function
KeccakAbsorb1024bits:

	mXorState512	apInput, apState, 0
	mXorState512	apInput, apState, 64
	mPushRegs
	mKeccakPermutation
	mPopRegs
	ret

# -------------------------------------------------------------------------

	.size	KeccakAbsorb1088bits, .-KeccakAbsorb1088bits
	.align	2
	.global	KeccakAbsorb1088bits
	.type	KeccakAbsorb1088bits, %function
KeccakAbsorb1088bits:

	mXorState512	apInput, apState, 0
	mXorState512	apInput, apState, 64
	movq		128(apInput), %rax
	xorq		%rax, 128(apState)
	mPushRegs
	mKeccakPermutation
	mPopRegs
	ret

# -------------------------------------------------------------------------

	.size	KeccakAbsorb1152bits, .-KeccakAbsorb1152bits
	.align	2
	.global	KeccakAbsorb1152bits
	.type	KeccakAbsorb1152bits, %function
KeccakAbsorb1152bits:

	mXorState512	apInput, apState, 0
	mXorState512	apInput, apState, 64
	mXorState128	apInput, apState, 128
	mPushRegs
	mKeccakPermutation
	mPopRegs
	ret

# -------------------------------------------------------------------------

	.size	KeccakAbsorb1344bits, .-KeccakAbsorb1344bits
	.align	2
	.global	KeccakAbsorb1344bits
	.type	KeccakAbsorb1344bits, %function
KeccakAbsorb1344bits:

	mXorState512	apInput, apState, 0
	mXorState512	apInput, apState, 64
	mXorState256	apInput, apState, 128
	movq		160(apInput), %rax
	xorq		%rax, 160(apState)
	mPushRegs
	mKeccakPermutation
	mPopRegs
	ret

# -------------------------------------------------------------------------

	.size	KeccakAbsorb, .-KeccakAbsorb
	.align	2
	.global	KeccakAbsorb
	.type	KeccakAbsorb, %function
KeccakAbsorb:

	movq		apState, xpState

	test		$16, aNbrWords
	jz		xorInputToState8
	mXorState512	apInput, xpState, 0
	mXorState512	apInput, xpState, 64
	addq		$128, apInput
	addq		$128, xpState

xorInputToState8:
	test		$8, aNbrWords
	jz		xorInputToState4
	mXorState512	apInput, xpState, 0
	addq		$64, apInput
	addq		$64, xpState

xorInputToState4:
	test		$4, aNbrWords
	jz		xorInputToState2
	mXorState256	apInput, xpState, 0
	addq		$32, apInput
	addq		$32, xpState

xorInputToState2:
	test		$2, aNbrWords
	jz		xorInputToState1
	mXorState128	apInput, xpState, 0
	addq		$16, apInput
	addq		$16, xpState

xorInputToState1:
	test		$1, aNbrWords
	jz		xorInputToStateDone
	movq		(apInput), %rax
	xorq		%rax, (xpState)

xorInputToStateDone:

	mPushRegs
	mKeccakPermutation
	mPopRegs
	ret

# -------------------------------------------------------------------------

	.size	KeccakInitializeState, .-KeccakInitializeState
	.align	2
	.global	KeccakInitializeState
	.type	KeccakInitializeState, %function
KeccakInitializeState:
	xorq		%rax, %rax
	xorq		%rcx, %rcx
	notq		%rcx

	.if 		UseSIMD == 0
	movq		%rax,   0*8(apState)
	movq		%rcx,   1*8(apState)
	movq		%rcx,   2*8(apState)
	movq		%rax,   3*8(apState)
	movq		%rax,   4*8(apState)
	movq		%rax,   5*8(apState)
	movq		%rax,   6*8(apState)
	movq		%rax,   7*8(apState)
	movq		%rcx,   8*8(apState)
	movq		%rax,   9*8(apState)
	movq		%rax,  10*8(apState)
	movq		%rax,  11*8(apState)
	movq		%rcx,  12*8(apState)
	movq		%rax,  13*8(apState)
	movq		%rax,  14*8(apState)
	movq		%rax,  15*8(apState)
	movq		%rax,  16*8(apState)
	movq		%rcx,  17*8(apState)
	movq		%rax,  18*8(apState)
	movq		%rax,  19*8(apState)
	movq		%rcx,  20*8(apState)
	movq		%rax,  21*8(apState)
	movq		%rax,  22*8(apState)
	movq		%rax,  23*8(apState)
	movq		%rax,  24*8(apState)
	.else
	pxor		%xmm0, %xmm0

	movq		%rax,   0*8(apState)
	movq		%rcx,   1*8(apState)
	movq		%rcx,   2*8(apState)
	movq		%rax,   3*8(apState)
	movdqu		%xmm0,  4*8(apState)
	movdqu		%xmm0,  6*8(apState)
	movq		%rcx,   8*8(apState)
	movq		%rax,   9*8(apState)
	movdqu		%xmm0, 10*8(apState)
	movq		%rcx,  12*8(apState)
	movq		%rax,  13*8(apState)
	movdqu		%xmm0, 14*8(apState)
	movq		%rax,  16*8(apState)
	movq		%rcx,  17*8(apState)
	movdqu		%xmm0, 18*8(apState)
	movq		%rcx,  20*8(apState)
	movq		%rax,  21*8(apState)
	movdqu		%xmm0, 22*8(apState)
	movq		%rax,  24*8(apState)
	.endif
	ret

# -------------------------------------------------------------------------

	.size	KeccakExtract1024bits, .-KeccakExtract1024bits
	.align	2
	.global	KeccakExtract1024bits
	.type	KeccakExtract1024bits, %function
KeccakExtract1024bits:

	movq		0*8(apState), %rax
	movq		1*8(apState), %rcx
	movq		2*8(apState), %rdx
	movq		3*8(apState), %r8
	notq		%rcx
	notq		%rdx
	movq		%rax, 0*8(%rsi)
	movq		%rcx, 1*8(%rsi)
	movq		%rdx, 2*8(%rsi)
	movq		%r8,  3*8(%rsi)

	movq		4*8(apState), %rax
	movq		5*8(apState), %rcx
	movq		6*8(apState), %rdx
	movq		7*8(apState), %r8
	movq		%rax, 4*8(%rsi)
	movq		%rcx, 5*8(%rsi)
	movq		%rdx, 6*8(%rsi)
	movq		%r8,  7*8(%rsi)

	movq		 8*8(apState), %rax
	movq		 9*8(apState), %rcx
	movq		10*8(apState), %rdx
	movq		11*8(apState), %r8
	notq		%rax
	movq		%rax,  8*8(%rsi)
	movq		%rcx,  9*8(%rsi)
	movq		%rdx, 10*8(%rsi)
	movq		%r8,  11*8(%rsi)

	movq		12*8(apState), %rax
	movq		13*8(apState), %rcx
	movq		14*8(apState), %rdx
	movq		15*8(apState), %r8
	notq		%rax
	movq		%rax, 12*8(%rsi)
	movq		%rcx, 13*8(%rsi)
	movq		%rdx, 14*8(%rsi)
	movq		%r8,  15*8(%rsi)
	ret

