.global fork
fork:
	push {r7}
	mov r7, #0x1
	svc 0
	bx lr
