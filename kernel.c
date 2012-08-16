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

void first(void) {
	bwputs("In user mode 1\n");
	if(!fork()) task();
	bwputs("In user mode 2\n");
	while(1);
}

#define STACK_SIZE 256 /* Size of task stacks in words */
#define TASK_LIMIT 2   /* Max number of tasks we can handle */

unsigned int *init_task(unsigned int *stack, void (*start)(void)) {
	stack += STACK_SIZE - 16; /* End of stack, minus what we're about to push */
	stack[0] = 0x10; /* User mode, interrupts on */
	stack[1] = (unsigned int)start;
	return stack;
}

int main(void) {
	unsigned int stacks[TASK_LIMIT][STACK_SIZE];
	unsigned int *tasks[TASK_LIMIT];
	size_t task_count = 0;
	size_t current_task = 0;

	*(PIC + VIC_INTENABLE) = PIC_TIMER01;

	*TIMER0 = 1000000;
	*(TIMER0 + TIMER_CONTROL) = TIMER_EN | TIMER_PERIODIC
	                            | TIMER_32BIT | TIMER_INTEN;

	tasks[task_count] = init_task(stacks[task_count], &first);
	task_count++;

	while(1) {
		tasks[current_task] = activate(tasks[current_task]);

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

		current_task++;
		if(current_task >= task_count) current_task = 0;
	}

	return 0;
}
