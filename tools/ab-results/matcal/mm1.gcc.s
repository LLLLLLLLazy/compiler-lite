	.file	"mm1.gcc.c"
	.option pic
	.text
	.section	.text.startup,"ax",@progbits
	.align	1
	.globl	main
	.type	main, @function
main:
	addi	sp,sp,-96
	sd	ra,88(sp)
	sd	s1,72(sp)
	sd	s2,64(sp)
	sd	s0,80(sp)
	sd	s3,56(sp)
	sd	s4,48(sp)
	sd	s5,40(sp)
	sd	s6,32(sp)
	sd	s7,24(sp)
	sd	s8,16(sp)
	sd	s9,8(sp)
	call	getint@plt
	mv	s1,a0
	call	getint@plt
	mv	s2,a0
	ble	s1,zero,.L2
	lla	s3,A
	sraiw	s4,s1,1
	li	s0,0
	mv	s5,s3
	li	s6,4096
	bgt	s4,s0,.L45
.L3:
	addiw	a5,s0,1
	add	s5,s5,s6
	beq	s1,a5,.L46
.L29:
	mv	s0,a5
	ble	s4,s0,.L3
.L45:
	mv	a0,s5
	call	getarray@plt
	addiw	a5,s0,1
	add	s5,s5,s6
	bne	s1,a5,.L29
.L46:
	lla	s6,B
	li	s5,0
	mv	s7,s6
	li	s8,4096
	ble	s4,s5,.L47
.L5:
	addiw	a5,s5,1
	add	s7,s7,s8
	beq	s5,s0,.L48
.L30:
	mv	s5,a5
	bgt	s4,s5,.L5
.L47:
	mv	a0,s7
	call	getarray@plt
	addiw	a5,s5,1
	add	s7,s7,s8
	bne	s5,s0,.L30
.L48:
	slli	s8,s0,32
	li	a0,31
	srli	s8,s8,32
	call	_sysy_starttime@plt
	addi	s9,s8,1
	li	s5,0
	slli	s9,s9,2
	lla	a5,A
	li	s7,4096
	ble	s4,s5,.L8
.L11:
	addiw	a4,s5,1
	add	a5,a5,s7
	beq	s5,s0,.L49
.L31:
	mv	s5,a4
	bgt	s4,s5,.L11
.L8:
	mv	a0,a5
	mv	a2,s9
	li	a1,255
	call	memset@plt
	mv	a5,a0
	addiw	a4,s5,1
	add	a5,a5,s7
	bne	s5,s0,.L31
.L49:
	li	s5,0
	lla	a5,B
	li	s7,4096
	bgt	s4,s5,.L12
.L14:
	addiw	a4,s5,1
	add	a5,a5,s7
	beq	s5,s0,.L50
.L32:
	mv	s5,a4
	ble	s4,s5,.L14
.L12:
	mv	a0,a5
	mv	a2,s9
	li	a1,255
	call	memset@plt
	mv	a5,a0
	addiw	a4,s5,1
	add	a5,a5,s7
	bne	s5,s0,.L32
.L50:
	slli	t6,s8,2
	lla	t3,C
	slli	s8,s8,12
	add	t5,s3,s7
	lla	a6,A+4
	add	a6,a6,t6
	add	t5,t5,s8
	mv	t4,t3
	lla	t1,A
	li	a7,4096
.L13:
	mv	a1,t4
	mv	a2,s6
	mv	a4,t1
.L15:
	lw	a0,0(a2)
	lw	a5,0(a4)
	addi	a4,a4,4
	slliw	a3,a0,1
	slliw	a5,a5,1
	addw	a3,a3,a0
	addw	a5,a5,a3
	sw	a5,0(a1)
	addi	a2,a2,4
	addi	a1,a1,4
	bne	a4,a6,.L15
	add	t1,t1,a7
	add	s6,s6,a7
	add	t4,t4,a7
	add	a6,a4,a7
	bne	t1,t5,.L13
	add	a1,t3,a7
	lla	a3,C+4
	add	a3,a3,t6
	add	a1,a1,s8
	lla	a0,C
	li	a2,3
	li	a6,4096
.L18:
	mv	a4,a0
.L17:
	lw	a5,0(a4)
	addi	a4,a4,4
	mulw	a5,a5,a5
	addiw	a5,a5,7
	divw	a5,a5,a2
	sw	a5,-4(a4)
	bne	a4,a3,.L17
	add	a0,a0,a6
	add	a3,a4,a6
	bne	a0,a1,.L18
	li	t5,0
	li	t6,0
	li	a7,4096
.L19:
	mv	t1,s3
	li	t4,0
.L23:
	mv	a2,t1
	mv	a3,t3
	li	a1,0
	li	a4,0
.L20:
	lw	a6,0(a3)
	lw	a5,0(a2)
	mv	a0,a4
	addi	a3,a3,4
	mulw	a5,a5,a6
	addiw	a4,a4,1
	add	a2,a2,a7
	addw	a1,a5,a1
	bgt	s0,a0,.L20
	add	a5,t1,t5
	sw	a1,0(a5)
	addi	t1,t1,4
	addiw	a5,t4,1
	ble	s0,t4,.L21
	mv	t4,a5
	j	.L23
.L21:
	addiw	a5,t6,1
	add	t5,t5,a7
	add	t3,t3,a7
	bge	t6,s0,.L22
	mv	t6,a5
	j	.L19
.L2:
	li	a0,31
	call	_sysy_starttime@plt
.L22:
	ble	s2,zero,.L33
	addiw	a5,s1,-1
	slli	a5,a5,32
	srli	a5,a5,32
	slli	a4,a5,2
	lla	a7,A+4
	slli	a5,a5,12
	lla	a0,A+4096
	lla	t1,A
	add	a7,a7,a4
	add	a0,a0,a5
	li	a6,0
	li	s0,0
	li	a1,4096
.L25:
	mv	a3,a7
	mv	a2,t1
	ble	s1,zero,.L27
.L28:
	mv	a4,a2
.L26:
	lw	a5,0(a4)
	addi	a4,a4,4
	mulw	a5,a5,a5
	addw	s0,a5,s0
	bne	a3,a4,.L26
	add	a2,a2,a1
	add	a3,a3,a1
	bne	a0,a2,.L28
.L27:
	addiw	a6,a6,1
	bne	s2,a6,.L25
.L24:
	li	a0,111
	call	_sysy_stoptime@plt
	mv	a0,s0
	call	putint@plt
	li	a0,10
	call	putch@plt
	ld	ra,88(sp)
	ld	s0,80(sp)
	ld	s1,72(sp)
	ld	s2,64(sp)
	ld	s3,56(sp)
	ld	s4,48(sp)
	ld	s5,40(sp)
	ld	s6,32(sp)
	ld	s7,24(sp)
	ld	s8,16(sp)
	ld	s9,8(sp)
	li	a0,0
	addi	sp,sp,96
	jr	ra
.L33:
	li	s0,0
	j	.L24
	.size	main, .-main
	.globl	C
	.globl	B
	.globl	A
	.bss
	.align	3
	.type	C, @object
	.size	C, 4194304
C:
	.zero	4194304
	.type	B, @object
	.size	B, 4194304
B:
	.zero	4194304
	.type	A, @object
	.size	A, 4194304
A:
	.zero	4194304
	.ident	"GCC: (Ubuntu 11.4.0-1ubuntu1~22.04) 11.4.0"
	.section	.note.GNU-stack,"",@progbits
