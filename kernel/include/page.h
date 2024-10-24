#ifndef PAGE_H
#define PAGE_H
#include <common.h>

#define PHSICS_UNIT     (1 << 24)           // 16M   
#define PAGE_NODE_NUM       (1 << 13)           // 1 + 2^1 + 2^2 + ... + 2^12 = 2^13 - 1 = 8191
#define MAX_PAGE_SIZE       PAGE_NODE_NUM * sizeof(Page_Node)       // 2^13 = 8192 
#ifdef DEBUG
#define MAX_PAGE_NUM        PHSICS_UNIT / MAX_PAGE_SIZE         // 2^11 = 2048
#else
#define MAX_PAGE_NUM        (1 << 8) - 1        // 255  jyy OJ 提供最大内存为4G, 此程序实际能处理最大内存 2^35 = 32G
#endif
#define PAGE_SIZE_TYPE      13        // 13 种页
#define PAGE_SIZE_OFFSET    12       
#define HAVE_BUBBLE         1
#define FULL_USED           0


typedef char* Page;
typedef char Page_Node;
typedef int Page_Index;

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


void page_system_init(void* begin, void* end);
void page_init(Page _page);
PAGE_SIZE trans_page_size(const size_t size);
void *page_alloc(size_t size);
void page_free(void *ptr);
void *alloc_space_from_page(Page _page, PAGE_SIZE req_size);
void *get_space_in_page(Page _page, Page_Index _index, PAGE_SIZE req_size, PAGE_SIZE base_size);
void free_space_in_page(const uintptr_t ptr, Page _page, Page_Index _index, PAGE_SIZE base_size);
Page_Index left_branch(const Page_Index index);
Page_Index right_branch(const Page_Index index);
void *convert_page_to_phsics(Page _page, Page_Index _index, PAGE_SIZE base_size);

#endif