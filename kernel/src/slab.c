#include <slab.h>

/**
 * 每个CPU拥有类型不一样的, 可能多个的 slab, 每种类型的 slab 使用链表连接
 * slabs_of_cpu[_cpu][slab_type] 代表特定的CPU '_cpu' 拥有的 'slab_type' 类型的 slab 的组成的链表的头
 */

Slab *slabs_of_cpu[MAX_CPU_NUM][SLAB_SIZE_TYPE] = { NULL };


SLAB_SIZE trans_slab_size(const size_t size){
    for(SLAB_SIZE stype = S_8B; stype < S_REFUSE; stype++){
        if(size <= (1 << stype)){
            return stype;
        }
    }
    return S_REFUSE;
}

void slab_system_init(){
    int cpu_num = cpu_count();
    for(int _cpu = 0; _cpu < cpu_num; _cpu++){
        for(SLAB_SIZE size = S_8B; size < S_REFUSE; size++){
            slab_init(_cpu, size);
        }
    }
}

Slab *slab_init(const int _cpu, const SLAB_SIZE size){
    // 从页表系统申请内存
    void *addr = page_alloc(SLAB_SPACE);
    if(addr == NULL){
        return NULL;
    }
#ifdef DEBUG
    assert(((uintptr_t)addr & ((1 << 12)-1)) == 0);
#else 
    if(((uintptr_t)addr & ((1 << 12)-1))) { return NULL; }
#endif

    // 格式化 slab 片, 将其按照固定大小分割
    Slab *_slab = (Slab*)addr;
    _slab->locked = SPIN_LOCK_INIT;
    _slab->cpu_id = _cpu;
    _slab->type = size;
    _slab->free_list = NULL;
    
    // slab 自身占用至少对齐到64B, 因为 sizeof(Slab) = 40 < 64
    uintptr_t slab_ocupy = align_ptr(sizeof(Slab), size >= S_64B ? size : S_64B);
    _slab->block_num = (SLAB_SPACE - slab_ocupy) >> size;
    _slab->free_block_num = _slab->block_num;
    int count = _slab->free_block_num;
    uintptr_t _block_addr = (uintptr_t)_slab + slab_ocupy;
    while(count > 0){
        Block *_block = (Block*)_block_addr;
        _block->next = _slab->free_list;
        _slab->free_list = _block;
        _block_addr += (1 << size);
        count --;
    }

    // 这里更改CPU的slab缓存池, 不需要锁, 因为只有约定的CPU会修改
    _slab->append = slabs_of_cpu[_cpu][size - SLAB_SIZE_OFFSET];
    slabs_of_cpu[_cpu][size - SLAB_SIZE_OFFSET] = _slab;

    return _slab;
}

void *slab_alloc(int _cpu, size_t size){
    SLAB_SIZE req_size = trans_slab_size(size);
    if(req_size == S_REFUSE){
        return page_alloc(size);
    }

    // 寻找合适的 slab
    Slab *_slab = slabs_of_cpu[_cpu][req_size - SLAB_SIZE_OFFSET];
    if(_slab == NULL){
        goto REQUEST_NEW_SLAB;
    }

    _lock(&(_slab->locked));
    while(1){
        if(_slab->free_block_num > 0){
            break;
        }
        else if(_slab->append == NULL){
            _unlock(&(_slab->locked));
            _slab = NULL;
            break;
        }
        /**
         * 先获得下一个slab的锁，再释放当前slab的锁
         * 这是出于slab的回收的考虑
         */
        _lock(&(_slab->append->locked));
        _unlock(&(_slab->locked));
        _slab = _slab->append;
    }

REQUEST_NEW_SLAB:
    if(_slab == NULL){
        // 没有空闲的 slab，申请一个新的
        _slab = slab_init(_cpu, req_size);
        if(_slab == NULL){
            return NULL;
        }
        _lock(&(_slab->locked));
        if(_slab->free_block_num <= 0){
            _unlock(&(_slab->locked));
            return NULL;
        }
    }

    // 从指定的 slab 中分配一块空间
    void *addr = alloc_block_from_slab(_slab);
    _unlock(&(_slab->locked));
#ifdef DEBUG
    assert(is_aligned(addr, req_size));
#else
    if(!is_aligned(addr, req_size)) { return NULL; }
#endif
    return addr;
}

void *alloc_block_from_slab(Slab *_slab){
#ifdef DEBUG
    assert(_slab != NULL);
    assert(_slab->free_block_num > 0);
#else
    if(_slab == NULL || _slab->free_block_num <= 0) { return NULL; }
#endif

    Block* _block = (Block*)_slab->free_list;
    _slab->free_list = _block->next;
    _slab->free_block_num -= 1;

    return (void*)_block;
}

void slab_free(void *ptr){
    uintptr_t mask = (1 << 12) - 1;
    uintptr_t slab_addr = (uintptr_t)ptr & (~mask);
#ifdef DEBUG
    assert(is_aligned((void*)slab_addr, 12));
#else
    if(!is_aligned((void*)slab_addr, 12)) { return ; }
#endif

    Slab *_slab = (Slab*)slab_addr;
    Block *_block = (Block*)ptr;

    _lock(&(_slab->locked));
    _block->next = _slab->free_list;
    _slab->free_list = _block;
    _slab->free_block_num += 1;
    // 触发 slab 缓存的回收
    if(_slab->free_block_num == _slab->block_num){
        recover_slab(_slab);
    } else {
        _unlock(&(_slab->locked));
    }
}

void recover_slab(Slab *_slab){
#ifdef DEBUG
    assert(_slab->free_block_num == _slab->block_num);
    assert(_try_lock(&(_slab->locked)) == false);
#else
    if(_slab->free_block_num < _slab->block_num || _try_lock(&(_slab->locked))){ 
        _unlock(&(_slab->locked));
        return; 
    }
#endif
    
    // 尝试回收 _slab. 注意此时此 _slab 是上锁状态
    Slab *slab_list_head = slabs_of_cpu[_slab->cpu_id][_slab->type - SLAB_SIZE_OFFSET];
    if(slab_list_head == _slab){
        _unlock(&(_slab->locked));
        return;
        // 链表表头第一个slab不予回收，否则会造成 slab_alloc 的崩溃
    }

    Slab *prev = slab_list_head;
    Slab *to_recover = prev->append;
    while(to_recover != NULL){
        if(to_recover == _slab){
            break;
        }
        prev = to_recover;
        to_recover = prev->append;
    }
#ifdef DEBUG
    assert(to_recover != NULL);
#else
    if(to_recover == NULL){ 
        _unlock(&(_slab->locked)); 
        return; 
    }
#endif

    if(!_try_lock(&(prev->locked))){
        /**
         * 有其他线程持有 to_recover 的前一个 prev
         * 可能是 alloc 函数持有 prev 并等待 to_recover
         * 可能是 prev 也正在被回收
         * 放弃回收, 为 to_recover 解锁
         */
        _unlock(&(_slab->locked));
        return;
    }

    // 回收
    prev->append = to_recover->append;
    page_free((void*)to_recover);
    _unlock(&(prev->locked));
}