.global fork
fork:
	push {r7}
	mov r7, #0x1
	svc 0
	bx lr
.global getpid
getpid:
	push {r7}
	mov r7, #0x2
	svc 0
	bx lr
.global write
write:
	push {r7}
	mov r7, #0x3
	svc 0
	bx lr
.global read
read:
	push {r7}
	mov r7, #0x4
	svc 0
	bx lr
.global interrupt_wait
interrupt_wait:
	push {r7}
	mov r7, #0x5
	svc 0
	bx lr
