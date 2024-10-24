#ifndef COMMON_H
#define COMMON_H

#include <kernel.h>
#include <klib.h>
#include <klib-macros.h>

#define MAX_CPU_NUM 8
#define SPIN_LOCK_INIT 0

void _lock(int *locked);
void _unlock(int *locked);
bool _try_lock(int *locked);
uintptr_t align_ptr(uintptr_t ptr, unsigned int align);
bool is_aligned(void* ptr, unsigned int align);

#endif