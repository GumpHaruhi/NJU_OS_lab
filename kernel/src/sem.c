#include <os.h>

extern spinlock_t sem_init_lock;

void sem_init(sem_t *sem, const char *name, int value){
#ifdef DEBUG_SEM
    printf("  >> sem init : %s  resource: %d\n", name, value);
#endif
    spin_lock(&sem_init_lock);

    memset(sem->name, '\0', NAME_LENTH);
    strcpy(sem->name, name);
    spin_init(&sem->lock, name);
    sem->resource = value;
    for(int i=0; i < SEM_CAPACITY; i++){
        sem->wait_queue[i] = NULL;
    }
    sem->queue_head = 0;
    sem->queue_tail = 0;

    spin_unlock(&sem_init_lock);
}

void sem_wait(sem_t *sem){
#ifdef DEBUG_SEM
    printf("  >> sem wait : %s  task: %s\n", sem->name, CUR_TASK->name);
    assert(sem != NULL);
#endif
    spin_lock(&sem->lock);
    
    if(sem->resource > 0){
        sem->resource --;
        spin_unlock(&sem->lock);
    }
    else{
        task_t *t = CUR_TASK;
        t->blocked = true;

        sem->wait_queue[sem->queue_tail] = t;
        sem->queue_tail = (sem->queue_tail + 1) % SEM_CAPACITY;

        spin_unlock(&sem->lock);

        while(t->blocked){
            yield();
        }
    }
}

void sem_signal(sem_t *sem){
    spin_lock(&sem->lock);

    if(sem->queue_head != sem->queue_tail && sem->resource == 0){
        sem->wait_queue[sem->queue_head]->blocked = false;
        sem->queue_head = (sem->queue_head + 1) % SEM_CAPACITY;
    } else {
        sem->resource ++;
    }

#ifdef DEBUG_SEM
    printf("  >> sem signal : %s  task: %s  now resource: %d\n", sem->name, CUR_TASK->name, sem->resource);
    assert(sem != NULL);
#endif

    spin_unlock(&sem->lock);
}