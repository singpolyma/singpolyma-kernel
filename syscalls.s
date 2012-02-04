.global syscall
syscall:
	push {r7}
	mov r7, #0x0
	svc 0
	pop {r7}
	bx lr

.global fork
fork:
	push {r7}
	mov r7, #0x1
	svc 0
	pop {r7}
	bx lr
