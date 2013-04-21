.type fork, %function
.global fork
fork:
	push {r7}
	mov r7, #0x1
	svc 0
	bx lr
.type getpid, %function
.global getpid
getpid:
	push {r7}
	mov r7, #0x2
	svc 0
	bx lr
.type write, %function
.global write
write:
	push {r7}
	mov r7, #0x3
	svc 0
	bx lr
.type read, %function
.global read
read:
	push {r7}
	mov r7, #0x4
	svc 0
	bx lr
.type interrupt_wait, %function
.global interrupt_wait
interrupt_wait:
	push {r7}
	mov r7, #0x5
	svc 0
	bx lr
