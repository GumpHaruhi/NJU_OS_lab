#include <common.h>
#include <page.h>

#define SLAB_SPACE (1 << 12)
#define SLAB_SIZE_OFFSET 3
#define SLAB_SIZE_TYPE 9

typedef enum SLAB_SIZE {
    S_8B = 3,
    S_16B,
    S_32B,
    S_64B,
    S_128B,
    S_256B,
    S_512B,
    S_1K,
    S_2K,
    S_REFUSE
} SLAB_SIZE ;

typedef struct BLOCK {
    struct BLOCK *next;
} Block ;

typedef struct SLAB {
    int locked;
    int cpu_id;
    int block_num;
    SLAB_SIZE type;
    Block *free_list;
    int free_block_num;
    struct SLAB *append;
} Slab ;


SLAB_SIZE trans_slab_size(const size_t size);
void slab_system_init();
Slab *slab_init(const int _cpu, const SLAB_SIZE size);
void *slab_alloc(int _cpu, size_t size);
void slab_free(void *ptr);
void *alloc_block_from_slab(Slab *_slab);
void recover_slab(Slab *_slab);