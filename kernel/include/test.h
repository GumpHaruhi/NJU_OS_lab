#ifdef DEBUG
#include <common.h>
#include <os.h>

#ifdef DEBUG_AB
void print_a(void *arg);
void print_b(void *arg);
#endif

#ifdef DEBUG_PV
void producer(void *arg);
void consumer(void *arg);
#endif

#endif