#include <common.h>
#include <stdint.h>
#include <string.h>
#include <page.h>
#include <slab.h>

static void *kalloc(size_t size) {
    bool itrpt_able = ienabled();
    iset(false);
    void *ptr = slab_alloc(cpu_current(), size);
    if(itrpt_able) {
        iset(true);
    }
    return ptr;
}

static void kfree(void *ptr) {
    bool itrpt_able = ienabled();
    iset(false);
    /**
     * 判断此地址是否对齐到 2**12 = 4096
     * 若对齐, 是物理分页. 否则是 slab 缓存
     */
    if(is_aligned(ptr, 12)){
        page_free(ptr);
    } else {
        slab_free(ptr);
    }
    
    if(itrpt_able){
        iset(true);
    }
}

static void pmm_init() {
#ifdef DEBUG
    uintptr_t pmsize = (
        (uintptr_t)heap.end
        - (uintptr_t)heap.start
    );
    printf(
        "Got %d MiB heap: [%p, %p)\n",
        pmsize >> 20, heap.start, heap.end
    );
#endif
    void *begin_addr = (void*)align_ptr((uintptr_t)heap.start, 24);
    page_system_init(begin_addr, heap.end);
    slab_system_init();
}


MODULE_DEF(pmm) = {
    .init  = pmm_init,
    .alloc = kalloc,
    .free  = kfree,
};
