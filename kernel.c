#include "versatilepb.h"
#include "asm.h"

void bwputs(char *s) {
	while(*s) {
		while(*(UART0 + UARTFR) & UARTFR_TXFF);
		*UART0 = *s;
		s++;
	}
}

void first(void) {
	bwputs("In user mode\n");
	while(1);
}

int main(void) {
	unsigned int first_stack[256];
	unsigned int *first_stack_start = first_stack + 256 - 16;
	first_stack_start[0] = 0x10;
	first_stack_start[1] = (unsigned int)&first;

	bwputs("Starting\n");
	activate(first_stack_start);

	while(1); /* We can't exit, there's nowhere to go */
	return 0;
}
