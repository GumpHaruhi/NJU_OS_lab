#include <os.h>
#include <common.h>

void _lock(int *locked){
    while( atomic_xchg(locked, 1) == 1) ;
}

void _unlock(int *locked){
    atomic_xchg(locked, 0);
}

bool _try_lock(int *locked){
    return atomic_xchg(locked, 1) == 0;
}

uintptr_t align_ptr(uintptr_t ptr, unsigned int align){
    /**
     * 将 num 对齐到 2**align
     */
    uintptr_t mask = (1 << align) - 1;
    if(((1 << align) & mask) != 0){
        // ERROR !
        printf("ptr %x align to %x mask %x failed\n", ptr, align, mask);
        panic("align failed");
    }

    if((ptr & mask) == 0){
        return ptr;
    }
    else{
        return (ptr & ~mask) + (1 << align);
    }
}

bool is_aligned(void* ptr, unsigned int align){
    unsigned int mask = (1 << align) - 1;
    return ((uintptr_t)ptr & mask) == 0;
}