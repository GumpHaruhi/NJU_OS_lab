#include <page.h>
// #include <os.h>

/**
 * page_locks[i] 是页框 page_pool[i] 的锁
 * real_page_num 是实际上的最大分页(16M)数量
 * begin_page_addr 是页框信息区的起始地址
 * begin_alloc_addr 是实际分配内存的起始地址
 */

Page page_pool[MAX_PAGE_NUM+1];
int page_locks[MAX_PAGE_NUM+1] = { SPIN_LOCK_INIT };
int real_page_num = 0;
uintptr_t begin_page_addr;
uintptr_t begin_alloc_addr;
int power_of_two[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096};


PAGE_SIZE trans_page_size(const size_t size){
    for(PAGE_SIZE ptype = P_4K; ptype < P_REFUSE; ptype++){
        if(size <= (1 << ptype)){
            return ptype;
        }
    }
    return P_REFUSE;
}

void page_system_init(void* begin, void* end){
#ifdef DEBUG
    assert(is_aligned(begin, 24));
#endif

    begin_page_addr = (uintptr_t)begin;
    uintptr_t phsics_gap = PHSICS_UNIT;
    begin_alloc_addr = (uintptr_t)begin + phsics_gap;
    uintptr_t _alloc_addr = begin_alloc_addr;
    uintptr_t _page_addr = begin_page_addr;
    int _index = 0;
    while(_alloc_addr + phsics_gap <= (uintptr_t)end){
        Page page = (Page)_page_addr;
        page_pool[_index] = page;
        page_init(page);
        _alloc_addr += phsics_gap;
        _page_addr += MAX_PAGE_SIZE;
        _index++;
    }
    real_page_num = _index;
}

void page_init(Page _page){
    Page_Index index = 1;
    PAGE_SIZE size = P_16M;
    while(size >= P_4K){
        for(uintptr_t i = (1 << (P_16M - size)); i > 0; i--){
            _page[index] = size;
            index++;
        }
        size--;
    }
}

void *page_alloc(size_t size){
    PAGE_SIZE req_size = trans_page_size(size);

    if(req_size == P_REFUSE){
        return NULL;
    }
#ifdef DEBUG
    assert(req_size <= P_16M && req_size >= P_4K);
#else
    if(req_size > P_16M || req_size < P_4K){ return NULL; }
#endif

    // 寻找一个有足够空间的页
    int _index = -1;
    for(int i = 0; i < real_page_num; i++){
        _lock(&(page_locks[i]));
        if(page_pool[i][1] >= req_size){
            _index = i;
            break;
        }
        _unlock(&(page_locks[i]));
    }

    if(_index == -1) { 
        return NULL; 
    }
#ifdef DEBUG
    assert(_index < real_page_num);
#else
    if(_index >= real_page_num){
        return NULL;
    }
#endif
    // 从这个页内分配地址
    Page page = page_pool[_index];
    void *addr = alloc_space_from_page(page, req_size);

    _unlock(&(page_locks[_index]));
    return addr;
}

void *alloc_space_from_page(Page _page, PAGE_SIZE req_size){
    void *addr = get_space_in_page(_page, 1, req_size, P_16M);
#ifdef DEBUG
    assert(is_aligned(addr, req_size));
#else
    if(!is_aligned(addr, req_size)){ return NULL; }
#endif
    return addr;
}

void *get_space_in_page(Page _page, Page_Index _index, PAGE_SIZE req_size, PAGE_SIZE base_size){
#ifdef DEBUG
    assert(_page != NULL);
    assert(req_size <= base_size);
#else
    if(_page == NULL || req_size > base_size || _index >= PAGE_NODE_NUM){ return NULL; }
#endif

    void *addr = NULL;

    // 当前页面的基础大小与需求大小相同
    if(base_size <= req_size){
        if(_page[_index] == req_size){
            // 当前页全部分配
            addr = convert_page_to_phsics(_page, _index, base_size);
            _page[_index] = FULL_USED;
            return addr;
        }
        else{
            return NULL;
        }
    }

    // 当前页面的基础大小大于需求，尝试下一级分页
    Page_Index left = left_branch(_index);
    Page_Index right = right_branch(_index);
    if(_page[left] >= req_size){
        addr = get_space_in_page(_page, left, req_size, base_size-1);
    }
    else {
        addr = get_space_in_page(_page, right, req_size, base_size-1);
    }

    if(addr != NULL){
        if(_page[_index] == req_size){
            /**
             * 这种情况说明有气泡: 当前页下没有完整的空闲子页了
             * 即: 从当前页的视角来看, 它区域内的空闲地址是不连续的
             * 即使页内还有空闲的碎片, 也应该标记为不可再分配
             */
            _page[_index] = HAVE_BUBBLE;
        }
        else{
            _page[_index] = _page[left] > _page[right] ? _page[left] : _page[right];
        }
    }

    return addr;
}

Page_Index left_branch(const Page_Index index){
    return index * 2;
}

Page_Index right_branch(const Page_Index index){
    return index * 2 + 1;
}

void *convert_page_to_phsics(Page _page, Page_Index _index, PAGE_SIZE base_size){
    // 将虚拟分页地址转换为实际分配的物理地址
    // 计算当前页指向的物理页帧地址
    uintptr_t page_gap_num = ((uintptr_t)_page - begin_page_addr) / MAX_PAGE_SIZE;
    uintptr_t phsics_offset = page_gap_num * PHSICS_UNIT;
    uintptr_t phsics_begin_addr = begin_alloc_addr + phsics_offset;
    // 计算页内偏移量
    Page_Index node_gap_num = _index - power_of_two[P_16M - base_size];
    uintptr_t node_offset = (1 << base_size) * node_gap_num;

    return (void*)(phsics_begin_addr + node_offset);
}

void page_free(void *ptr){
    uintptr_t mask = (1 << P_16M) -1;
    uintptr_t phsics_begin_addr = (uintptr_t)ptr & (~mask);
    int page_num = (phsics_begin_addr - begin_alloc_addr) >> P_16M;
#ifdef DEBUG
    assert(is_aligned(ptr, 12));
    assert(page_num < real_page_num);
#else
    if(page_num >= real_page_num) { return ; }
#endif
    Page _page = page_pool[page_num];

    _lock(&(page_locks[page_num]));
    free_space_in_page((uintptr_t)ptr, _page, 1, P_16M);
    _unlock(&(page_locks[page_num]));
}

void free_space_in_page(const uintptr_t ptr, Page _page, Page_Index _index, PAGE_SIZE base_size){
#ifdef DEBUG
    assert(_index < PAGE_NODE_NUM);
    assert(base_size >= P_4K);
    assert(base_size != P_REFUSE);
#else
    if(_index >= PAGE_NODE_NUM || base_size < P_4K || base_size == P_REFUSE){ return; }
#endif

    uintptr_t phsics_addr = (uintptr_t)convert_page_to_phsics(_page, _index, base_size);
    if(phsics_addr + (1 << base_size) > ptr) { return ; }
    Page_Index left = left_branch(_index);
    Page_Index right = right_branch(_index);

    if(phsics_addr == ptr){
        if(_page[_index] == FULL_USED){
            // 就是当前物理页
            _page[_index] = base_size;
            return ;
        }
        else{
            // 是当前页的左子页
            free_space_in_page(ptr, _page, left, base_size-1);
        }
    }
    else if(phsics_addr + (1 << (base_size-1)) > ptr){
        // 在当前页的左子页
        free_space_in_page(ptr, _page, left, base_size-1);
    }
    else {
        // 在当前页的右子页
        free_space_in_page(ptr, _page, right, base_size-1);
    }

    if(_page[left] == base_size-1 && _page[right] == base_size-1){
        // 当前页是干净的
        _page[_index] = base_size;
    }
    else{
        // 当前页仍是脏的
        _page[_index] = _page[left] > _page[right] ? _page[left] : _page[right];
    }
#ifdef DEBUG
    assert(_page[_index] != HAVE_BUBBLE);
    assert(_page[_index] != FULL_USED);
#endif
}