#ifndef TEXUGOS_THREADS_H
#define TEXUGOS_THREADS_H

typedef void (*thread_entry_t)(void *argument);

int thread_create(thread_entry_t entry, void *argument);
void thread_yield(void);
void thread_preempt(void);
unsigned int scheduler_run(void);

#endif
