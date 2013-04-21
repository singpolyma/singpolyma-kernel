.global irq_entry
irq_entry:
	/* Save user state */
	msr CPSR_c, #0xDF /* System mode */

	push {r7} /* Just like in syscall wrapper */
	/* Load PIC status */
	ldr r7, PIC
	ldr r7, [r7]
	PIC: .word 0x10140000
	/* Most significant bit index */
	clz r7, r7
	sub r7, r7, #31

	push {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,fp,ip,lr}
	mov r0, sp

	msr CPSR_c, #0xD2 /* IRQ mode */
	mrs ip, SPSR
	sub lr, lr, #0x4 /* lr is address of next instruction */
	stmfd r0!, {ip,lr}

	msr CPSR_c, #0xD3 /* Supervisor mode */

	/* Load kernel state */
	pop {r4,r5,r6,r7,r8,r9,r10,fp,ip,lr}
	mov sp, ip
	bx lr

.global svc_entry
svc_entry:
	/* Save user state */
	msr CPSR_c, #0xDF /* System mode */
	push {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,fp,ip,lr}
	mov r0, sp
	msr CPSR_c, #0xD3 /* Supervisor mode */

	mrs ip, SPSR
	stmfd r0!, {ip,lr}

	/* Load kernel state */
	pop {r4,r5,r6,r7,r8,r9,r10,fp,ip,lr}
	mov sp, ip
	bx lr

.type activate, %function
.global activate
activate:
	/* Save kernel state */
	mov ip, sp
	push {r4,r5,r6,r7,r8,r9,r10,fp,ip,lr}

	ldmfd r0!, {ip,lr}
	msr SPSR, ip

	msr CPSR_c, #0xDF /* System mode */
	mov sp, r0
	pop {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,fp,ip,lr}
	pop {r7}
	msr CPSR_c, #0xD3 /* Supervisor mode */

	movs pc, lr
