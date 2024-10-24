#ifdef DEBUG
#include <test.h>

#ifdef DEBUG_AB
void print_a(void *arg){
    printf("A");
}
void print_b(void *arg){
    printf("B");
}
#endif

#ifdef DEBUG_PV
extern sem_t empty;
extern sem_t full;

void producer(void *arg){
    sem_wait(&empty);
    printf("(");
    sem_signal(&full);
}

void consumer(void *arg){
    sem_wait(&full);
    printf(")");
    sem_signal(&empty);
}
#endif

#endif