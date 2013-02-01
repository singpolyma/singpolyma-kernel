unsigned int *activate(unsigned int *stack);
int fork(void);
int getpid(void);
int write(int fd, const void *buf, size_t count);
int read(int fd, void *buf, size_t count);
void interrupt_wait(int intr);
