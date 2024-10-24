#include <common.h>
#include <stdint.h>
#include <os.h>

CPU cpu_vector[MAX_CPU];
task_t *task_list = NULL;
int task_count;
task_t *idle_tasks[MAX_CPU];
spinlock_t sem_init_lock;
spinlock_t task_list_lock;

#define POLL_TIMES task_count*2 + 2

Context *kmt_context_save(Event ev, Context *context){
    if(CUR_TASK == idle_tasks[cpu_current()]){
        CUR_TASK->context[0] = context;
    } else {
        CUR_TASK->context[CUR_TASK->i_nest++] = context;
    }

    task_t *last_task = CUR_CPU.last_task;
    if(last_task && last_task != idle_tasks[cpu_current()] && last_task != CUR_TASK){
        spin_lock(&last_task->lock);
        last_task->running = false;
        last_task->i_nest --;
        spin_unlock(&last_task->lock);
    }

    CUR_CPU.last_task = CUR_CPU.current_task;
    return NULL;
}

Context *kmt_schedule(Event ev, Context *context){
    if(CUR_TASK == NULL){
        CUR_TASK = idle_tasks[cpu_current()];
    }

    task_t *t = CUR_TASK;
    // 进程的调度
    for(int i=0; i < POLL_TIMES; i++){
        if(t->next != NULL){
            t = t->next;
        } else {
            t = task_list;
        }

        if(t == CUR_TASK){
            if(t->blocked){
                continue;
            }
            t->i_nest--;
            CUR_TASK = t;
            return CUR_TASK->context[CUR_TASK->i_nest];
        }
        
        spin_lock(&t->lock);
        if(t->running || t->blocked){
            spin_unlock(&t->lock);
            continue;
        }
        t->running = true;
        spin_unlock(&t->lock);
#ifdef DEBUG
        printf("==== schedule task ====\n  name: %s\n", t->name);
#endif
        CUR_TASK = t;
        return CUR_TASK->context[CUR_TASK->i_nest];
    }

    CUR_TASK = idle_tasks[cpu_current()];
    return CUR_TASK->context[0];
}

static int create(task_t *task, const char *name, void (*entry)(void *arg), void *arg){
    spin_lock(&task_list_lock);
    
    memset(task->name, '\0', NAME_LENTH);
    memset(task->stack, '\0', STACK_SIZE);
    strcpy(task->name, name);

    task->context[0] = kcontext((Area){(void*)task->stack, (void*)(task->stack + STACK_SIZE)}, entry, arg);
    spin_init(&(task->lock), name);
    task->blocked = false;
    task->running = false;
    task->i_nest = 0;

    task_count ++;
    task->next = task_list;
    task_list = task;
#ifdef DEBUG
    printf("==== create new task ====\n  name: %s\n", task->name);
#endif
    spin_unlock(&task_list_lock);
    return 1;
}

static void teardown(task_t *task){
    for(int i=0; i < MAX_CPU; i++){
        if(task == idle_tasks[i]){
            return;
        }
    }

    spin_lock(&task_list_lock);
    if(task == task_list){
        task_list = task_list->next;
    } else {
        task_t *t = task_list;
        while(t != NULL){
            spin_lock(&t->lock);
            if(t->next == task){
                t->next = task->next;
                spin_unlock(&t->lock);
                break;
            }
            spin_unlock(&t->lock);
            t = t->next;
        }
    }
    
    pmm->free(task);
    task_count--;
    spin_unlock(&task_list_lock);
}

void cpu_idle(void *arg){
    while(1){
        yield();
    }
}

void init_cpu(CPU *_cpu){
    if(_cpu == NULL) { return; }

    _cpu->i_enable = true; 
    _cpu->i_nest_num = 0;
    _cpu->current_task = NULL;
    _cpu->last_task = NULL;
}

void init_idle_task(task_t *idle){
    idle = pmm->alloc(sizeof(task_t));

    memset(idle->name, '\0', NAME_LENTH);
    memset(idle->stack, '\0', STACK_SIZE);
    strcpy(idle->name, "idle");
    idle->context[0] = kcontext((Area){(void*)idle->stack, (void*)(idle->stack + STACK_SIZE)}, cpu_idle, NULL);
    spin_init(&idle->lock, "idle");
    idle->blocked = false;
    idle->running = false;
    idle->i_nest = 0;
    idle->next = NULL;
}

static void kmt_init(){
#ifdef DEBUG
    printf("==== KMT init ====\n  total cpu: %d\n", cpu_count());
#endif  

    spin_init(&task_list_lock, "task_list_lock");
    spin_init(&sem_init_lock, "sem_init_lock");

    os->on_irq(SEQ_MIN, EVENT_NULL, kmt_context_save);
    os->on_irq(SEQ_MAX, EVENT_NULL, kmt_schedule);

    for(int i=0; i < cpu_count(); i++){
        init_cpu(&(cpu_vector[i]));
        init_idle_task(idle_tasks[i]);
    }

    task_count = 0;
    task_list = NULL;
}

MODULE_DEF(kmt) = {
    .init  = kmt_init,
    .create = create,
    .teardown  = teardown,
    .spin_init = spin_init,
    .spin_lock = spin_lock,
    .spin_unlock = spin_unlock,
    .sem_init = sem_init,
    .sem_wait = sem_wait,
    .sem_signal = sem_signal
};