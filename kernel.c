#include <stddef.h>

#include "versatilepb.h"
#include "asm.h"

void *memcpy(void *dest, const void *src, size_t n) {
	char *d = dest;
	const char *s = src;
	size_t i;
	for(i = 0; i < n; i++) {
		d[i] = s[i];
	}
	return d;
}

void bwputs(char *s) {
	while(*s) {
		while(*(UART0 + UARTFR) & UARTFR_TXFF);
		*UART0 = *s;
		s++;
	}
}

void task(void) {
	bwputs("In other task\n");
	while(1);
}

#define STACK_SIZE 1024 /* Size of task stacks in words */
#define TASK_LIMIT 2   /* Max number of tasks we can handle */
#define PIPE_BUF   512 /* Size of largest atomic pipe message */
#define PIPE_LIMIT (TASK_LIMIT*5)

#define TASK_READY      0
#define TASK_WAIT_READ  1
#define TASK_WAIT_WRITE 2

void first(void) {
	bwputs("In user mode 1\n");
	if(!fork()) task();
	bwputs("In user mode 2\n");
	while(1);
}

struct pipe_ringbuffer {
	int start;
	int end;
	char data[PIPE_BUF];
};

#define RB_PUSH(rb, size, v) do { \
		(rb).data[(rb).end] = (v); \
		(rb).end++; \
		if((rb).end > size) (rb).end = 0; \
	} while(0)

#define RB_POP(rb, size, v) do { \
		(v) = (rb).data[(rb).start]; \
		(rb).start++; \
		if((rb).start > size) (rb).start = 0; \
	} while(0)

#define RB_LEN(rb, size) (((rb).end - (rb).start) + \
	(((rb).end < (rb).start) ? size : 0))

#define PIPE_PUSH(pipe, v) RB_PUSH((pipe), PIPE_BUF, (v))
#define PIPE_POP(pipe, v)  RB_POP((pipe), PIPE_BUF, (v))
#define PIPE_LEN(pipe)     (RB_LEN((pipe), PIPE_BUF))

unsigned int *init_task(unsigned int *stack, void (*start)(void)) {
	stack += STACK_SIZE - 16; /* End of stack, minus what we're about to push */
	stack[0] = 0x10; /* User mode, interrupts on */
	stack[1] = (unsigned int)start;
	return stack;
}

int main(void) {
	unsigned int stacks[TASK_LIMIT][STACK_SIZE];
	unsigned int *tasks[TASK_LIMIT];
	struct pipe_ringbuffer pipes[PIPE_LIMIT];
	size_t task_count = 0;
	size_t current_task = 0;
	size_t i;

	*(PIC + VIC_INTENABLE) = PIC_TIMER01;

	*TIMER0 = 1000000;
	*(TIMER0 + TIMER_CONTROL) = TIMER_EN | TIMER_PERIODIC
	                            | TIMER_32BIT | TIMER_INTEN;

	tasks[task_count] = init_task(stacks[task_count], &first);
	task_count++;

	/* Initialize all pipes */
	for(i = 0; i < PIPE_LIMIT; i++) {
		pipes[i].start = pipes[i].end = 0;
	}

	while(1) {
		tasks[current_task] = activate(tasks[current_task]);
		tasks[current_task][-1] = TASK_READY;

		switch(tasks[current_task][2+7]) {
			case 0x1: /* fork */
				if(task_count == TASK_LIMIT) {
					/* Cannot create a new task, return error */
					tasks[current_task][2+0] = -1;
				} else {
					/* Compute how much of the stack is used */
					size_t used = stacks[current_task] + STACK_SIZE
					              - tasks[current_task];
					/* New stack is END - used */
					tasks[task_count] = stacks[task_count] + STACK_SIZE - used;
					/* Copy only the used part of the stack */
					memcpy(tasks[task_count], tasks[current_task],
					       used*sizeof(*tasks[current_task]));
					/* Set return values in each process */
					tasks[current_task][2+0] = task_count;
					tasks[task_count][2+0] = 0;
					/* There is now one more task */
					task_count++;
				}
				break;
			case 0x2: /* getpid */
				tasks[current_task][2+0] = current_task;
				break;
			case -4: /* Timer 0 or 1 went off */
				if(*(TIMER0 + TIMER_MIS)) { /* Timer0 went off */
					*(TIMER0 + TIMER_INTCLR) = 1; /* Clear interrupt */
				}
		}

		/* Select next TASK_READY task */
		while(TASK_READY != tasks[current_task =
			(current_task+1 >= task_count ? 0 : current_task+1)][-1]);
	}

	return 0;
}
