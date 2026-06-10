# .arch riscv
# .option pic0
.attribute arch, "rv64gcv"
.option nopic
.text
.comm A, 4194304, 4
.comm B, 4194304, 4
.comm C, 4194304, 4
.align 1
.global main
.type main, %function
main:
	addi sp,sp,-80
	sd ra,72(sp)
	sd s1,64(sp)
	sd s2,56(sp)
	sd s3,48(sp)
	sd s4,40(sp)
	sd s5,32(sp)
	sd s6,24(sp)
	sd s7,16(sp)
	sd s8,8(sp)

	sd ra,72(sp)
	call getint
	ld ra,72(sp)
	mv s1,a0
	sd ra,72(sp)
	call getint
	ld ra,72(sp)
	mv s7,a0
	la s2,A
	mv s5,s2
	li s3,0
.L_main_bb3:
	bge s3,s1,.L_main_bb8

	sraiw t3,s1,31
	srliw t3,t3,31
	addw t0,s1,t3
	sraiw t0,t0,1
	bge s3,t0,.L_main_bb13

	mv a0,s5
	sd ra,72(sp)
	call getarray
	ld ra,72(sp)
.L_main_bb13:
	addiw s3,s3,1
	lui t3,1
	add s5,s5,t3
	j .L_main_bb3
.L_main_bb8:
	la s8,B
	mv s6,s8
	li s4,0
.L_main_bb16:
	bge s4,s1,.L_main_bb21

	sraiw t3,s1,31
	srliw t3,t3,31
	addw t0,s1,t3
	sraiw t0,t0,1
	bge s4,t0,.L_main_bb22
	j .L_main_bb25
.L_main_bb21:
	li a0,25
	sd ra,72(sp)
	call _sysy_starttime
	ld ra,72(sp)
	mv a3,s2
	li a2,0
	j .L_main_bb28
.L_main_bb22:
	mv a0,s6
	sd ra,72(sp)
	call getarray
	ld ra,72(sp)
.L_main_bb25:
	addiw s4,s4,1
	lui t3,1
	add s6,s6,t3
	j .L_main_bb16
.L_main_bb28:
	bge a2,s1,.L_main_bb46

	sraiw t3,s1,31
	srliw t3,t3,31
	addw t0,s1,t3
	sraiw t0,t0,1
	blt a2,t0,.L_main_bb35

	mv a1,a3
	li a0,0
	j .L_main_bb38
.L_main_bb35:
	addiw a2,a2,1
	lui t3,1
	add a3,a3,t3
	j .L_main_bb28
.L_main_bb38:
	bge a0,s1,.L_main_bb35

	subw t1,s1,a0
	vsetvli t0,t1,e32, m1, tu, ma
	li t3,-1
	vmv.v.x v1,t3
	vse32.v v1,(a1)
	addw a0,a0,t0
	slli t3,t0,2
	add a1,a1,t3
	j .L_main_bb38
.L_main_bb46:
	mv a3,s8
	li a2,0
.L_main_bb47:
	bge a2,s1,.L_main_bb52

	sraiw t3,s1,31
	srliw t3,t3,31
	addw t0,s1,t3
	sraiw t0,t0,1
	blt a2,t0,.L_main_bb54
	j .L_main_bb56
.L_main_bb52:
	la s4,C
	mv t5,s8
	mv a7,s2
	mv a6,s4
	li a5,0
	j .L_main_bb67
.L_main_bb54:
	mv a1,a3
	li a0,0
	j .L_main_bb59
.L_main_bb56:
	addiw a2,a2,1
	lui t3,1
	add a3,a3,t3
	j .L_main_bb47
.L_main_bb59:
	bge a0,s1,.L_main_bb56

	subw t1,s1,a0
	vsetvli t0,t1,e32, m1, tu, ma
	li t3,-1
	vmv.v.x v1,t3
	vse32.v v1,(a1)
	addw a0,a0,t0
	slli t3,t0,2
	add a1,a1,t3
	j .L_main_bb59
.L_main_bb67:
	bge a5,s1,.L_main_bb90

	mv a2,a6
	mv a3,a7
	mv a4,t5
	li a1,0
.L_main_bb73:
	bge a1,s1,.L_main_bb85

	lw t0,0(a3)
	slliw t2,t0,1
	lw t0,0(a4)
	slliw t1,t0,1
	addw t1,t1,t0
	addw t0,t2,t1
	sw t0,0(a2)
	addiw a1,a1,1
	addi a2,a2,4
	addi a3,a3,4
	addi a4,a4,4
	j .L_main_bb73
.L_main_bb85:
	addiw a5,a5,1
	lui t3,1
	add a6,a6,t3
	lui t3,1
	add a7,a7,t3
	lui t3,1
	add t5,t5,t3
	j .L_main_bb67
.L_main_bb90:
	mv a2,s4
	li a1,0
.L_main_bb91:
	bge a1,s1,.L_main_bb107

	mv t2,a2
	li a0,0
.L_main_bb95:
	bge a0,s1,.L_main_bb104

	lw t0,0(t2)
	mulw t1,t0,t0
	li t0,7
	addw t0,t1,t0
	lui t3,349525
	addiw t3,t3,1366
	mul t1,t0,t3
	srai t1,t1,32
	srliw t3,t1,31
	addw t1,t1,t3
	sw t1,0(t2)
	addiw a0,a0,1
	addi t2,t2,4
	j .L_main_bb95
.L_main_bb104:
	addiw a1,a1,1
	lui t3,1
	add a2,a2,t3
	j .L_main_bb91
.L_main_bb107:
	mv s3,s2
	mv t6,s4
	li t5,0
.L_main_bb108:
	bge t5,s1,.L_main_bb134

	mv a6,s3
	mv a7,t6
	li a5,0
.L_main_bb113:
	blt a5,s1,.L_main_bb119
.L_main_bb115:
	addiw t5,t5,1
	lui t3,1
	add t6,t6,t3
	lui t3,1
	add s3,s3,t3
	j .L_main_bb108
.L_main_bb119:
	mv a4,s2
	mv a2,a7
	li a3,0
	li a1,0
.L_main_bb120:
	bge a1,s1,.L_main_bb131

	lw t2,0(a2)
	slli t3,a5,2
	add t0,a4,t3
	lw t1,0(t0)
	mulw t0,t2,t1
	addw a3,a3,t0
	addiw a1,a1,1
	addi a2,a2,4
	lui t3,1
	add a4,a4,t3
	j .L_main_bb120
.L_main_bb131:
	sw a3,0(a6)
	addiw a5,a5,1
	addi a6,a6,4
	j .L_main_bb113
.L_main_bb134:
	li a6,0
	li s3,0
.L_main_bb135:
	blt a6,s7,.L_main_bb138
.L_main_bb137:
	li a0,105
	sd ra,72(sp)
	call _sysy_stoptime
	ld ra,72(sp)
	mv a0,s3
	sd ra,72(sp)
	call putint
	ld ra,72(sp)
	li a0,10
	sd ra,72(sp)
	call putch
	ld ra,72(sp)
	li a0,0
	ld s8,8(sp)
	ld s7,16(sp)
	ld s6,24(sp)
	ld s5,32(sp)
	ld s4,40(sp)
	ld s3,48(sp)
	ld s2,56(sp)
	ld s1,64(sp)
	ld ra,72(sp)
	addi sp,sp,80
	ret
.L_main_bb138:
	mv a5,s2
	mv a4,s3
	li a3,0
.L_main_bb139:
	bge a3,s1,.L_main_bb143

	mv a1,a5
	li a0,0
	mv a2,a4
	j .L_main_bb145
.L_main_bb143:
	addiw a6,a6,1
	mv s3,a4
	j .L_main_bb135
.L_main_bb145:
	bge a0,s1,.L_main_bb153

	lw t0,0(a1)
	mulw t1,t0,t0
	addw a2,a2,t1
	addiw a0,a0,1
	addi a1,a1,4
	j .L_main_bb145
.L_main_bb153:
	addiw a3,a3,1
	lui t3,1
	add a5,a5,t3
	mv a4,a2
	j .L_main_bb139
.size main, .-main
