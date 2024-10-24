<center><font size=6>LAB_1 Phsics Memory Manage</font></center>



**姓名：**Gump

**学号：**221900398

---------

## PAGE 分页式管理内存

将内存视作一些特定大小的页：最小 4K 一页，最大 16M 一页

两个相同大小的、连续的页可以被视作一个大一级的页

```c
typedef enum PAGE_SIZE{
    P_4K = 12,
    P_8K,
    P_16K,
    P_32K,
    P_64K,
    P_128K,
    P_256K,
    P_512K,
    P_1M,
    P_2M,
    P_4M,
    P_8M,
    P_16M,
    P_REFUSE = 25
} PAGE_SIZE ;
```

类似于“位图”的思想，为了维护这些物理页的信息：一个大小为 sizeof(char) 的数代表一个页的信息

将所得内存`heap.start ---> heap.end` 按照最大页规格划分：每 16M `1 << 24` 为一段：

```
    | 16M | 16M | 16M | ..... | 16M |
	|							    |
heap->start 				    heap->end
```

除第一段外的其他内存用来分配使用，**第一段 16M 用于储存其他物理页的使用信息**

为了能够记录一片最大物理页的详细使用情况，必须为其可能的每一片子页都分配空间去记录。因此一片16M的物理页的虚拟页需要这样记录：

```
| 16M | 8M | 8M | 4M | 4M | 4M | 4M | 2M | 2M |....| 4K | 4K |
每一段大小是 sizeof(char) = 1B
共 1 + 2**1 + 2**2 + ... + 2**12 = 2**13 -1 = 8191B
对齐到2**13 = 8192B

那么一个 16M 的空间可以容纳 2**11 个 16M 虚拟页
即此数据结构最多能够支持 2**11 * 2**24 = 2**35 = 32G 的内存
```

**每一段虚拟页的数据是其所指代的物理页的当前可分配空间，数据类型是 PAGE_SIZE**

- 从虚拟页到物理页的地址转换：

```C
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
```



> 虚拟页并非实际意义上的虚拟化，只是记录物理页信息的数据结构
>
> 物理页框的设计消除了内存分配的外部碎片，但是会有内部气泡



## SLAB 每CPU缓存

物理页框 page 的最小标准为 4K 一页，slab 用于分配大小介于 8B ~ 2K 的内存。

- **每CPU设计：**每个CPU都有独属于其的slab缓存池。一个slab只服务于特定的CPU。在分配内存的时候可以避免并发问题，避免自旋等待

- slab 管辖下的空闲内存块通过链表连接。相同类型的 slab 也通过链表连接

```c
typedef struct BLOCK {
    struct BLOCK *next;
} Block ;

typedef struct SLAB {
    int locked;
    int cpu_id;
    int block_num;
    enum SLAB_SIZE type;
    struct Block *free_list;
    int free_block_num;
    struct SLAB *append;
} Slab ;
```



## 如何回收 SLAB ？

先阐释几个事实：

- 完全空闲的 slab 需要被回收，归还到物理分页系统
- 由一个CPU申请的内存，可能被另一个CPU释放
- 要先获得该 slab 的锁，才能回收它
- 如果一个 slab 被回收，同时有其他进程正在自旋等待该 slab，会造成访问越界，表现为 Undefined Behavior 

为此设计出 _try_lock 函数：

```C
bool _try_lock(int *locked){
    return atomic_xchg(locked, 1) == 0;
}
```

对于待回收的 slab ：

```C
void recover_slab(Slab *_slab){
    // 此时已经持有待回收的slab的锁 _slab->locked
    // 找到待回收的slab在链表中的上一个slab: prev
    Slab *to_recover = _slab;
    Slab *prev;
    
    // 尝试获得 prev 的锁
    if(!_try_lock(&(prev->locked))){
        /**
         * 有其他进程持有 to_recover 的前一个 prev
         * 可能是 alloc 函数持有 prev 并等待 to_recover
         * 可能是 prev 也正在被回收
         * 放弃回收，为 to_recover 解锁
         */
        _unlock(&(_slab->locked));
        return;
    }

    // 回收
    prev->append = to_recover->append;
    page_free((void*)to_recover);
    _unlock(&(prev->locked));
}
```

在 `slab_alloc` 中：**遍历链表查找空闲的 slab 时，必须在获得下一个 slab 的锁之后，再释放当前 slab 的锁。即使只有特定的一个CPU会访问此链表：**

```c
void *slab_alloc(size_t size){
    // slab_list_head 是链表表头
	if(slab_list_head == NULL){
        goto REQUEST_NEW_SLAB;
    }
	
    Slab *_slab = slab_list_head;
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
        _lock(&(_slab->append->locked)); // 先获得下一个slab的锁
        _unlock(&(_slab->locked)); // 再释放当前slab锁
        _slab = _slab->append;
    }

REQUEST_NEW_SLAB:
    /** 其他逻辑 **/
}
```



------------------------

## 失败的设计

在一开始，我的设计是：每个CPU都有一个任务队列，新创建的任务直接加入到当前CPU的任务队列里。在每次中断处理后的进程调度环节，均衡所有CPU的任务负载，使得每个CPU都有大致平均的任务数量。这样调度的时候，每个CPU直接在自己的任务队列中选择任务，*且不需要为任务上锁（就是为了这点醋包的饺子）*

```c
struct CPU {
    ...
    int task_num;   // 所持有的任务数量
    task_t *task_list;  // 任务队列
};
// 在调度函数中 
Context *kmt_schedule(Event ev, Context *context){
    if(CUR_TASK == NULL){
        CUR_CPU.current_task = idle_task[cpu_current()];
    }

    load_balance();   // 均衡每个CPU的负载
    // 选择一个任务
    // ...
}

void load_balance(){
    if(cpu_count() < 2){ return; }
    spin_lock(&task_schedule_lock);
    
    // load_sort 是CPU按负载从大到小的排序
    CPU *load_sort[MAX_CPU]

    int average = total_task / cpu_count();
    int mod = total_task % cpu_count();
    for(int i=0; i < cpu_count(); i++){
        if(load_sort[i]->task_num <= average + mod){
            // no need balance
            break;
        }
        for(int j=cpu_count(); j > i; j--){
            if(load_sort[i]->task_num <= average + mod){
                break;
            } else if(load_sort[j]->task_num >= average){
                continue;
            }
            move_task_between_cpu(load_sort[i], load_sort[j], average - load_sort[j]->task_num);
        }
    }
    spin_unlock(&task_schedule_lock);
}

void move_task_between_cpu(CPU *_from, CPU *_to, int count){
    // 从_from 上移动count个CPU到_to上
}
```

但是之后发现测试标准：每个线程在一段时间内要在每个CPU上出现过。遂推到此设计重写

> 但我认为每CPU任务队列的效率或许更高：
>
> 1.负载均衡有门槛，不会发生频繁的负载均衡
>
> 2.避免在调度的时候对任务上锁（自旋锁）



## 难忘的BUG

在一开始，我的代码都是这种形式：

```c
...
    int cpu_id = cpu_current();
	cpu_vector[cpu_id].current_task ...
...    
```

总会发生奇怪的问题。之后改为使用宏，解决

```c
// os.h
extern CPU cpu_vector[MAX_CPU]; 
#define CUR_CPU cpu_vector[cpu_current()]
#define CUR_TASK CUR_CPU.current_task

// kmt.c
...
    CUR_CPU ...
    CUR_TASK ...
...
```



## 设计亮点

- **中断嵌套的处理**

> 什么是中断嵌套？

```c
下面这种情况发生了中断嵌套
====task A====|中断|====task B===(yield/其他CPU运行A)===task A====|中断|
下面就没有中断嵌套
====task A====|中断|====task A====
====task A====|中断|====task B====|中断|
```

为了处理中断嵌套，设计 task_t , CPU : 

```c
struct task {
    ...
    Context *context[4];   // 上下文数组
    int i_nest;    // 中断嵌套的层数
};

struct CPU {
    task_t* last_task;   // 指向最近一次中断前运行的任务
};
```

在中断处理的第一个处理程序 `kmt_context_save` 中，代码：

```c
Context *kmt_context_save(Event ev, Context *context){
    CUR_TASK->context[CUR_TASK->i_nest++] = context;

    task_t *last_task = CUR_CPU.last_task;
    if(last_task && last_task != idle_tasks[cpu_current()] && last_task != CUR_TASK){
        spin_lock(&last_task->lock);
        last_task->running = false;
        last_task->i_nest --;          // 嵌套层数减1，即未发生嵌套
        spin_unlock(&last_task->lock);
    }

    CUR_CPU.last_task = CUR_CPU.current_task;   // 追踪任务
    return NULL;
}
```



- **可睡眠的信号量与FIFO队列**

每个信号量维护一个先进先出的等待队列。当一个任务需要等待此信号量时，将其加入队列睡眠，而不是自旋等待

```c
struct semaphore {
    char name[NAME_LENTH];
    spinlock_t lock;
    volatile int resource;
    task_t *wait_queue[SEM_CAPACITY];   // FIFO queue
    volatile int queue_head;
    volatile int queue_tail;
};

void sem_wait(sem_t *sem){
    spin_lock(&sem->lock);
    
    if(sem->resource > 0){    // 资源充足
        sem->resource --;
        spin_unlock(&sem->lock);
    }
    else{     // 资源不足
        task_t *t = CUR_TASK;
        t->blocked = true;     // 阻塞此任务
		// 加入到等待队列	
        sem->wait_queue[sem->queue_tail] = t;
        sem->queue_tail = (sem->queue_tail + 1) % SEM_CAPACITY;
		// 释放此信号量，不自旋等待
        spin_unlock(&sem->lock);

        while(t->blocked){
            // yield() 切换阻塞的任务，实行睡眠
            yield();
        }
    }
}
```

