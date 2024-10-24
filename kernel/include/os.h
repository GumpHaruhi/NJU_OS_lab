#ifndef __OS_H__
#define __OS_H__
#include <common.h>

#define MAX_CPU 8
#define NAME_LENTH 64
#define STACK_SIZE 4096
#define MAX_TASK 1024
#define SEM_CAPACITY 256
#define SEQ_MIN (1 << 31)
#define SEQ_MAX ~(1 << 31)

typedef struct CPU CPU;
typedef struct interrupt_request IRQ;

struct CPU {
    int i_nest_num;
    bool i_enable;
    task_t* current_task;
    task_t* last_task;
};

extern CPU cpu_vector[MAX_CPU];
#define CUR_CPU cpu_vector[cpu_current()]
#define CUR_TASK CUR_CPU.current_task

struct interrupt_request {
  int seq;
  int event;
  handler_t handler;
  IRQ* next;
};

struct spinlock {
    int lock;
    char name[NAME_LENTH];
};

struct task {
    char name[NAME_LENTH];
    uint8_t stack[STACK_SIZE];
    spinlock_t lock;
    Context *context[4];
    int i_nest;
    bool blocked;
    bool running;
    task_t *next;
};

struct semaphore {
    char name[NAME_LENTH];
    spinlock_t lock;
    volatile int resource;
    task_t *wait_queue[SEM_CAPACITY];   // FIFO queue
    volatile int queue_head;
    volatile int queue_tail;
};

void spin_init(spinlock_t *lk, const char *name);
void spin_lock(spinlock_t *lk);
void spin_unlock(spinlock_t *lk);
void sem_init(sem_t *sem, const char *name, int value);
void sem_wait(sem_t *sem);
void sem_signal(sem_t *sem);
void interrupt_off();
void interrupt_on();
#endif