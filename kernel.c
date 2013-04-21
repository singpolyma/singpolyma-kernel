#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "versatilepb.h"
#include "asm.h"

void bwputs(char *s) {
	while(*s) {
		while(*(UART0 + UARTFR) & UARTFR_TXFF);
		*UART0 = *s;
		s++;
	}
}

#define STACK_SIZE 1024 /* Size of task stacks in words */
#define TASK_LIMIT 10  /* Max number of tasks we can handle */
#define PIPE_BUF   512 /* Size of largest atomic pipe message */
#define PATH_MAX   255 /* Longest absolute path */
#define PIPE_LIMIT (TASK_LIMIT*5)

#define PATHSERVER_FD (TASK_LIMIT+3) /* File descriptor of pipe to pathserver */

#define TASK_READY      0
#define TASK_WAIT_READ  1
#define TASK_WAIT_WRITE 2
#define TASK_WAIT_INTR  3

/* This pathserver assumes that all files are FIFOs that were registered
   with mkfifo.  It also assumes a global tables of FDs shared by all
   processes.  It would have to get much smarter to be generally useful.
	The first TASK_LIMIT FDs are reserved for use by their respective tasks.
	0-2 are reserved FDs and are skipped.
	The server registers itself at /sys/pathserver
*/
void pathserver(void) {
	char paths[PIPE_LIMIT - TASK_LIMIT - 3][PATH_MAX];
	int npaths = 0;
	int i = 0;
	unsigned int plen = 0;
	unsigned int replyfd = 0;
	char path[PATH_MAX];

	memcpy(paths[npaths++], "/sys/pathserver", sizeof("/sys/pathserver"));

	while(1) {
		read(PATHSERVER_FD, &replyfd, 4);
		read(PATHSERVER_FD, &plen, 4);
		read(PATHSERVER_FD, path, plen);

		if(!replyfd) { /* mkfifo */
			memcpy(paths[npaths++], path, plen);
		} else { /* open */
			/* Search for path */
			for(i = 0; i < npaths; i++) {
				if(*paths[i] && strcmp(path, paths[i]) == 0) {
					i += 3; /* 0-2 are reserved */
					i += TASK_LIMIT; /* FDs reserved for tasks */
					write(replyfd, &i, 4);
					i = 0;
					break;
				}
			}

			if(i >= npaths) {
				i = -1; /* Error: not found */
				write(replyfd, &i, 4);
			}
		}
	}
}

int mkfifo(const char *pathname, int mode) {
	size_t plen = strlen(pathname)+1;
	char buf[4+4+PATH_MAX];
	(void)mode;

	*((unsigned int*)buf) = 0;
	*((unsigned int*)(buf+4)) = plen;
	memcpy(buf+4+4, pathname, plen);
	write(PATHSERVER_FD, buf, 4+4+plen);

	/* XXX: no error handling */
	return 0;
}

int open(const char *pathname, int flags) {
	unsigned int replyfd = getpid() + 3;
	size_t plen = strlen(pathname)+1;
	unsigned int fd = -1;
	char buf[4+4+PATH_MAX];
	(void)flags;

	*((unsigned int*)buf) = replyfd;
	*((unsigned int*)(buf+4)) = plen;
	memcpy(buf+4+4, pathname, plen);
	write(PATHSERVER_FD, buf, 4+4+plen);
	read(replyfd, &fd, 4);

	return fd;
}

void serialout(volatile unsigned int* uart, unsigned int intr) {
	int fd;
	char c;
	int doread = 1;
	mkfifo("/dev/tty0/out", 0);
	fd = open("/dev/tty0/out", 0);

	/* enable TX interrupt on UART */
	*(uart + UARTIMSC) |= UARTIMSC_TXIM;

	while(1) {
		if(doread) read(fd, &c, 1);
		doread = 0;
		if(!(*(uart + UARTFR) & UARTFR_TXFF)) {
			*uart = c;
			doread = 1;
		}
		interrupt_wait(intr);
		*(uart + UARTICR) = UARTICR_TXIC;
	}
}

void serialin(volatile unsigned int* uart, unsigned int intr) {
	int fd;
	char c;
	mkfifo("/dev/tty0/in", 0);
	fd = open("/dev/tty0/in", 0);

	/* enable RX interrupt on UART */
	*(uart + UARTIMSC) |= UARTIMSC_RXIM;

	while(1) {
		interrupt_wait(intr);
		*(uart + UARTICR) = UARTICR_RXIC;
		if(!(*(uart + UARTFR) & UARTFR_RXFE)) {
			c = *uart;
			write(fd, &c, 1);
		}
	}
}

void echo(void) {
	int fdout, fdin;
	char c;
	fdout = open("/dev/tty0/out", 0);
	fdin = open("/dev/tty0/in", 0);

	while(1) {
		read(fdin, &c, 1);
		write(fdout, &c, 1);
	}
}

void first(void) {
	if(!fork()) pathserver();
	if(!fork()) serialout(UART0, PIC_UART0);
	if(!fork()) serialin(UART0, PIC_UART0);
	if(!fork()) echo();

	while(1);
}

struct pipe_ringbuffer {
	int start;
	int end;
	char data[PIPE_BUF];
};

#define RB_PUSH(rb, size, v) do { \
		(rb).data[(rb).end] = (v); \
		(rb).end = ((rb).end + 1) % (size); \
	} while(0)

#define RB_POP(rb, size, v) do { \
		(v) = (rb).data[(rb).start]; \
		(rb).start = ((rb).start + 1) % (size); \
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

void _read(unsigned int *task, unsigned int **tasks, size_t task_count, struct pipe_ringbuffer *pipes);
void _write(unsigned int *task, unsigned int **tasks, size_t task_count, struct pipe_ringbuffer *pipes);

void _read(unsigned int *task, unsigned int **tasks, size_t task_count, struct pipe_ringbuffer *pipes) {
	task[-1] = TASK_READY;
	/* If the fd is invalid, or trying to read too much  */
	if(task[2+0] > PIPE_LIMIT || task[2+2] > PIPE_BUF) {
		task[2+0] = -1;
	} else {
		struct pipe_ringbuffer *pipe = &pipes[task[2+0]];
		if((size_t)PIPE_LEN(*pipe) < task[2+2]) {
			/* Trying to read more than there is: block */
			task[-1] = TASK_WAIT_READ;
		} else {
			size_t i;
			char *buf = (char*)task[2+1];
			/* Copy data into buf */
			for(i = 0; i < task[2+2]; i++) {
				PIPE_POP(*pipe,buf[i]);
			}

			/* Unblock any waiting writes
				XXX: nondeterministic unblock order
			*/
			for(i = 0; i < task_count; i++) {
				if(tasks[i][-1] == TASK_WAIT_WRITE) {
					_write(tasks[i], tasks, task_count, pipes);
				}
			}
		}
	}
}

void _write(unsigned int *task, unsigned int **tasks, size_t task_count, struct pipe_ringbuffer *pipes) {
	/* If the fd is invalid or the write would be non-atomic */
	if(task[2+0] > PIPE_LIMIT || task[2+2] > PIPE_BUF) {
		task[2+0] = -1;
	} else {
		struct pipe_ringbuffer *pipe = &pipes[task[2+0]];

		if((size_t)PIPE_BUF - PIPE_LEN(*pipe) <
			task[2+2]) {
			/* Trying to write more than we have space for: block */
			task[-1] = TASK_WAIT_WRITE;
		} else {
			size_t i;
			const char *buf = (const char*)task[2+1];
			/* Copy data into pipe */
			for(i = 0; i < task[2+2]; i++) {
				PIPE_PUSH(*pipe,buf[i]);
			}

			/* Unblock any waiting reads
				XXX: nondeterministic unblock order
			*/
			for(i = 0; i < task_count; i++) {
				if(tasks[i][-1] == TASK_WAIT_READ) {
					_read(tasks[i], tasks, task_count, pipes);
				}
			}
		}
	}
}

int main(void) {
	unsigned int stacks[TASK_LIMIT][STACK_SIZE];
	unsigned int *tasks[TASK_LIMIT];
	struct pipe_ringbuffer pipes[PIPE_LIMIT];
	size_t task_count = 0;
	size_t current_task = 0;
	size_t i;

	*(PIC + VIC_INTENABLE) = PIC_TIMER01;

	*TIMER0 = 10000;
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
			case 0x3: /* write */
				_write(tasks[current_task], tasks, task_count, pipes);
				break;
			case 0x4: /* read */
				_read(tasks[current_task], tasks, task_count, pipes);
				break;
			case 0x5: /* interrupt_wait */
				/* Enable interrupt */
				*(PIC + VIC_INTENABLE) = tasks[current_task][2+0];
				/* Block task waiting for interrupt to happen */
				tasks[current_task][-1] = TASK_WAIT_INTR;
				break;
			default: /* Catch all interrupts */
				if((int)tasks[current_task][2+7] < 0) {
					unsigned int intr = (1 << -tasks[current_task][2+7]);

					if(intr == PIC_TIMER01) {
						/* Never disable timer. We need it for pre-emption */
						if(*(TIMER0 + TIMER_MIS)) { /* Timer0 went off */
							*(TIMER0 + TIMER_INTCLR) = 1; /* Clear interrupt */
						}
					} else {
						/* Disable interrupt, interrupt_wait re-enables */
						*(PIC + VIC_INTENCLEAR) = intr;
					}
					/* Unblock any waiting tasks
						XXX: nondeterministic unblock order
					*/
					for(i = 0; i < task_count; i++) {
						if(tasks[i][-1] == TASK_WAIT_INTR && tasks[i][2+0] == intr) {
							tasks[i][-1] = TASK_READY;
						}
					}
				}
		}

		/* Select next TASK_READY task */
		while(TASK_READY != tasks[current_task = (current_task + 1) % task_count][-1]);
	}

	return 0;
}
