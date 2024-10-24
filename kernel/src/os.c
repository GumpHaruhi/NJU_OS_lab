#include <common.h>
#include <os.h>
#ifdef DEBUG
#include <test.h>
#endif

extern CPU cpu_vector[MAX_CPU];
IRQ *irq_list = NULL;

void on_irq(int seq, int event, handler_t handler){
    IRQ *_irq = pmm->alloc(sizeof(IRQ));
    _irq->seq = seq;
    _irq->event = event;
    _irq->handler = handler;
    _irq->next = NULL;

    if(irq_list == NULL){
        irq_list = _irq;
        return;
    } 
    else if(_irq->seq < irq_list->seq){
        _irq->next = irq_list;
        irq_list = _irq;
        return;
    }

    IRQ *i_req = irq_list;
    while(i_req->next != NULL){
        if(_irq->seq < i_req->next->seq){
            _irq->next = i_req->next;
            i_req->next = _irq;
            return;
        }
        i_req = i_req->next;
    }
    i_req->next = _irq;
#ifdef DEBUG
    panic_on(!irq_list, "irq_list is NULL after an on_irq");
#endif
}

Context *trap(Event ev, Context *context) {
#ifdef DEBUG
    panic_on(!irq_list, "irq_list is NULL but OS trap");
#else
    if(irq_list == NULL) { return NULL; }
#endif
    Context *next_ctx = NULL;
    IRQ *req = irq_list;
    while(req != NULL){
        if(req->event == EVENT_NULL || req->event == ev.event){
            Context *ret_ctx = req->handler(ev, context);
#ifdef DEBUG
            if(ev.event == EVENT_SYSCALL){
                return ret_ctx;
            }
#endif
            if(ret_ctx){
                next_ctx = ret_ctx;
            }
        }
        req = req->next;
    }

#ifdef DEBUG
    panic_on(!next_ctx, "trap will return a NULL context");
#endif
    return next_ctx;
}

#ifdef DEBUG_PV
sem_t empty;
sem_t full;
#endif

static void os_init() {
    pmm->init();
    kmt->init();
    dev->init();

#ifdef DEBUG_AB
    kmt->create(pmm->alloc(sizeof(task_t)), "print_a", print_a, NULL);
    kmt->create(pmm->alloc(sizeof(task_t)), "print_b", print_b, NULL);
    kmt->create(pmm->alloc(sizeof(task_t)), "print_b", print_b, NULL);
#endif

#ifdef DEBUG_PV
    kmt->sem_init(&empty, "empty", 1);
    kmt->sem_init(&full, "full", 0);

    kmt->create(pmm->alloc(sizeof(task_t)), "producer", producer, NULL);
    kmt->create(pmm->alloc(sizeof(task_t)), "consumer", consumer, NULL);
#endif

#ifdef DEBUG
    printf("==== OS init over ====\n");
#endif
}

static void os_run() {
    iset(true);
    while (1) {
        ;
        yield();
    }
}

MODULE_DEF(os) = {
    .init = os_init,
    .run  = os_run,
    .on_irq = on_irq,
    .trap = trap,
};

void interrupt_off(){
    bool old_status = ienabled();
    iset(false);
    if(CUR_CPU.i_nest_num == 0){
        CUR_CPU.i_enable = old_status;
    }
    CUR_CPU.i_nest_num += 1;
}

void interrupt_on(){
#ifdef DEBUG
    assert(ienabled() == false);
    if(CUR_CPU.i_nest_num <= 0){
        panic("interrupt has been open");
    }
#else
    if(ienabled()){ return; }
    if(CUR_CPU.i_nest_num <= 0){ return; }
#endif
    CUR_CPU.i_nest_num --;
    if(CUR_CPU.i_nest_num == 0 && CUR_CPU.i_enable){
        iset(true);
    }
}