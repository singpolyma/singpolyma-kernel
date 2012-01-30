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
	bwputs("Starting\n");
	activate();

	while(1); /* We can't exit, there's nowhere to go */
	return 0;
}
