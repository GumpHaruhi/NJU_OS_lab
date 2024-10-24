#include <os.h>

void spin_init(spinlock_t *lk, const char *name){
    lk->lock = SPIN_LOCK_INIT;
    memset(lk->name, '\0', NAME_LENTH);
    strcpy(lk->name, name);
}

void spin_lock(spinlock_t *lk){
    interrupt_off();
    _lock(&(lk->lock));
}

void spin_unlock(spinlock_t *lk){
    _unlock(&(lk->lock));
    interrupt_on();
}
