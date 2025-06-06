#include <PR/ultratypes.h>
#ifndef TARGET_N64
#include <string.h>
#endif
#ifdef USE_SYSTEM_MALLOC
#include <stdlib.h>

#ifdef TARGET_WII_U
#include <malloc.h>
#else
#define memalign(a, n) malloc(n)
#endif
#endif

#include "sm64.h"

#define INCLUDED_FROM_MEMORY_C

#include "buffers/buffers.h"
#include "decompress.h"
#include "game_init.h"
#include "main.h"
#include "memory.h"
#include "segment_symbols.h"
#include "segments.h"
#include "platform_info.h"

// round up to the next multiple
#define ALIGN4(val) (((val) + 0x3) & ~0x3)
#define ALIGN8(val) (((val) + 0x7) & ~0x7)
#define ALIGN16(val) (((val) + 0xF) & ~0xF)

struct MainPoolState {
#ifndef USE_SYSTEM_MALLOC
    u32 freeSpace;
    struct MainPoolBlock *listHeadL;
    struct MainPoolBlock *listHeadR;
#endif
    struct MainPoolState *prev;
};

struct MainPoolBlock {
    struct MainPoolBlock *prev;
    struct MainPoolBlock *next;
#ifdef USE_SYSTEM_MALLOC
    void (*releaseHandler)(void *addr);
#endif
};

#ifdef USE_SYSTEM_MALLOC
struct AllocOnlyPoolBlock {
    struct AllocOnlyPoolBlock *prev;
#if !IS_64_BIT
    void *pad; // require 8 bytes alignment
#endif
};

struct AllocOnlyPool {
    struct AllocOnlyPoolBlock *lastBlock;
    u32 lastBlockSize;
    u32 lastBlockNextPos;
};

struct FreeListNode {
    struct FreeListNode *next;
};

struct AllocatedNode {
    s32 bin;
    s32 pad;
};
#else
struct MemoryBlock {
    struct MemoryBlock *next;
    u32 size;
};
#endif

struct MemoryPool {
#ifdef USE_SYSTEM_MALLOC
    struct AllocOnlyPool *allocOnlyPool;
    struct FreeListNode *bins[27];
#else
    u32 totalSpace;
    struct MemoryBlock *firstBlock;
    struct MemoryBlock freeList;
#endif
};

extern uintptr_t sSegmentTable[32];
extern u32 sPoolFreeSpace;
extern u8 *sPoolStart;
extern u8 *sPoolEnd;
extern struct MainPoolBlock *sPoolListHeadL;
extern struct MainPoolBlock *sPoolListHeadR;


/**
 * Memory pool for small graphical effects that aren't connected to Objects.
 * Used for colored text, paintings, and environmental snow and bubbles.
 */
struct MemoryPool *gEffectsMemoryPool;

uintptr_t sSegmentTable[32];
u32 sPoolFreeSpace;
u8 *sPoolStart;
u8 *sPoolEnd;
struct MainPoolBlock *sPoolListHeadL;
struct MainPoolBlock *sPoolListHeadR;


static struct MainPoolState *gMainPoolState = NULL;

uintptr_t set_segment_base_addr(s32 segment, void *addr) {
    sSegmentTable[segment] = (uintptr_t) addr & 0x1FFFFFFF;
    return sSegmentTable[segment];
}

void *get_segment_base_addr(s32 segment) {
    return (void *) (sSegmentTable[segment] | 0x80000000);
}

#ifndef NO_SEGMENTED_MEMORY
void *segmented_to_virtual(const void *addr) {
    size_t segment = (uintptr_t) addr >> 24;
    size_t offset = (uintptr_t) addr & 0x00FFFFFF;

    return (void *) ((sSegmentTable[segment] + offset) | 0x80000000);
}

void *virtual_to_segmented(u32 segment, const void *addr) {
    size_t offset = ((uintptr_t) addr & 0x1FFFFFFF) - sSegmentTable[segment];

    return (void *) ((segment << 24) + offset);
}

void move_segment_table_to_dmem(void) {
    s32 i;

    for (i = 0; i < 16; i++) {
        gSPSegment(gDisplayListHead++, i, sSegmentTable[i]);
    }
}
#else
void *segmented_to_virtual(const void *addr) {
    return (void *) addr;
}

void *virtual_to_segmented(UNUSED u32 segment, const void *addr) {
    return (void *) addr;
}

void move_segment_table_to_dmem(void) {
}
#endif

#ifdef USE_SYSTEM_MALLOC
static void main_pool_free_all(void) {
    while (sPoolListHeadL != NULL) {
        main_pool_free(sPoolListHeadL + 1);
    }
}

void main_pool_init(void) {
    atexit(main_pool_free_all);
}
#else

/**
 * Initialize the main memory pool. This pool is conceptually a pair of stacks
 * that grow inward from the left and right. It therefore only supports
 * freeing the object that was most recently allocated from a side.
 */
void main_pool_init(void *start, void *end) {
    sPoolStart = (u8 *) ALIGN16((uintptr_t) start) + 16;
    sPoolEnd = (u8 *) ALIGN16((uintptr_t) end - 15) - 16;
    sPoolFreeSpace = sPoolEnd - sPoolStart;

    sPoolListHeadL = (struct MainPoolBlock *) (sPoolStart - 16);
    sPoolListHeadR = (struct MainPoolBlock *) sPoolEnd;
    sPoolListHeadL->prev = NULL;
    sPoolListHeadL->next = NULL;
    sPoolListHeadR->prev = NULL;
    sPoolListHeadR->next = NULL;
}
#endif

#ifdef USE_SYSTEM_MALLOC
void *main_pool_alloc(u32 size, void (*releaseHandler)(void *addr)) {
    struct MainPoolBlock *newListHead = (struct MainPoolBlock *) memalign(64, sizeof(struct MainPoolBlock) + size);
    if (newListHead == NULL) {
        abort();
    }
    if (sPoolListHeadL != NULL) {
        sPoolListHeadL->next = newListHead;
    }
    newListHead->prev = sPoolListHeadL;
    newListHead->next = NULL;
    newListHead->releaseHandler = releaseHandler;
    sPoolListHeadL = newListHead;
    return newListHead + 1;
}

u32 main_pool_free(void *addr) {
    struct MainPoolBlock *block = ((struct MainPoolBlock *) addr) - 1;
    void *toFree;
    do {
        if (sPoolListHeadL == NULL) {
            abort();
        }
        if (sPoolListHeadL->releaseHandler != NULL) {
            sPoolListHeadL->releaseHandler(sPoolListHeadL + 1);
        }
        toFree = sPoolListHeadL;
        sPoolListHeadL = sPoolListHeadL->prev;
        if (sPoolListHeadL != NULL) {
            sPoolListHeadL->next = NULL;
        }
        free(toFree);
    } while (toFree != block);
    return 0;
}

u32 main_pool_push_state(void) {
    struct MainPoolState *prevState = gMainPoolState;
    gMainPoolState = main_pool_alloc(sizeof(*gMainPoolState), NULL);
    gMainPoolState->prev = prevState;
    return 0;
}

/**
 * Restore pool state from a previous call to main_pool_push_state. Return the
 * amount of free space left in the pool.
 */
u32 main_pool_pop_state(void) {
    struct MainPoolState *prevState = gMainPoolState->prev;
    main_pool_free(gMainPoolState);
    gMainPoolState = prevState;
}
#else
/**
 * Allocate a block of memory from the pool of given size, and from the
 * specified side of the pool (MEMORY_POOL_LEFT or MEMORY_POOL_RIGHT).
 * If there is not enough space, return NULL.
 */
void *main_pool_alloc(u32 size, u32 side) {
    struct MainPoolBlock *newListHead;
    void *addr = NULL;

    size = ALIGN16(size) + 16;
    if (size != 0 && sPoolFreeSpace >= size) {
        sPoolFreeSpace -= size;
        if (side == MEMORY_POOL_LEFT) {
            newListHead = (struct MainPoolBlock *) ((u8 *) sPoolListHeadL + size);
            sPoolListHeadL->next = newListHead;
            newListHead->prev = sPoolListHeadL;
            newListHead->next = NULL;
            addr = (u8 *) sPoolListHeadL + 16;
            sPoolListHeadL = newListHead;
        } else {
            newListHead = (struct MainPoolBlock *) ((u8 *) sPoolListHeadR - size);
            sPoolListHeadR->prev = newListHead;
            newListHead->next = sPoolListHeadR;
            newListHead->prev = NULL;
            sPoolListHeadR = newListHead;
            addr = (u8 *) sPoolListHeadR + 16;
        }
    }
    return addr;
}

/**
 * Free a block of memory that was allocated from the pool. The block must be
 * the most recently allocated block from its end of the pool, otherwise all
 * newer blocks are freed as well.
 * Return the amount of free space left in the pool.
 */
u32 main_pool_free(void *addr) {
    struct MainPoolBlock *block = (struct MainPoolBlock *) ((u8 *) addr - 16);
    struct MainPoolBlock *oldListHead = (struct MainPoolBlock *) ((u8 *) addr - 16);

    if (oldListHead < sPoolListHeadL) {
        while (oldListHead->next != NULL) {
            oldListHead = oldListHead->next;
        }
        sPoolListHeadL = block;
        sPoolListHeadL->next = NULL;
        sPoolFreeSpace += (uintptr_t) oldListHead - (uintptr_t) sPoolListHeadL;
    } else {
        while (oldListHead->prev != NULL) {
            oldListHead = oldListHead->prev;
        }
        sPoolListHeadR = block->next;
        sPoolListHeadR->prev = NULL;
        sPoolFreeSpace += (uintptr_t) sPoolListHeadR - (uintptr_t) oldListHead;
    }
    return sPoolFreeSpace;
}

/**
 * Resize a block of memory that was allocated from the left side of the pool.
 * If the block is increasing in size, it must be the most recently allocated
 * block from the left side.
 * The block does not move.
 */
void *main_pool_realloc(void *addr, u32 size) {
    void *newAddr = NULL;
    struct MainPoolBlock *block = (struct MainPoolBlock *) ((u8 *) addr - 16);

    if (block->next == sPoolListHeadL) {
        main_pool_free(addr);
        newAddr = main_pool_alloc(size, MEMORY_POOL_LEFT);
    }
    return newAddr;
}

/**
 * Return the size of the largest block that can currently be allocated from the
 * pool.
 */
u32 main_pool_available(void) {
    return sPoolFreeSpace - 16;
}

/**
 * Push pool state, to be restored later. Return the amount of free space left
 * in the pool.
 */
u32 main_pool_push_state(void) {
    struct MainPoolState *prevState = gMainPoolState;
    u32 freeSpace = sPoolFreeSpace;
    struct MainPoolBlock *lhead = sPoolListHeadL;
    struct MainPoolBlock *rhead = sPoolListHeadR;

    gMainPoolState = main_pool_alloc(sizeof(*gMainPoolState), MEMORY_POOL_LEFT);
    gMainPoolState->freeSpace = freeSpace;
    gMainPoolState->listHeadL = lhead;
    gMainPoolState->listHeadR = rhead;
    gMainPoolState->prev = prevState;
    return sPoolFreeSpace;
}

/**
 * Restore pool state from a previous call to main_pool_push_state. Return the
 * amount of free space left in the pool.
 */
u32 main_pool_pop_state(void) {
    sPoolFreeSpace = gMainPoolState->freeSpace;
    sPoolListHeadL = gMainPoolState->listHeadL;
    sPoolListHeadR = gMainPoolState->listHeadR;
    gMainPoolState = gMainPoolState->prev;
    return sPoolFreeSpace;
}
#endif

/**
 * Perform a DMA read from ROM. The transfer is split into 4KB blocks, and this
 * function blocks until completion.
 */
static void dma_read(u8 *dest, u8 *srcStart, u8 *srcEnd) {
#ifdef TARGET_N64
    u32 size = ALIGN16(srcEnd - srcStart);
    osInvalDCache(dest, size);
    while (size != 0) {
        u32 copySize = (size >= 0x1000) ? 0x1000 : size;

        osPiStartDma(&gDmaIoMesg, OS_MESG_PRI_NORMAL, OS_READ, (uintptr_t) srcStart, dest, copySize,
                     &gDmaMesgQueue);
        osRecvMesg(&gDmaMesgQueue, &gMainReceivedMesg, OS_MESG_BLOCK);

        dest += copySize;
        srcStart += copySize;
        size -= copySize;
    }
#else
    memcpy(dest, srcStart, srcEnd - srcStart);
#endif
}

/**
 * Perform a DMA read from ROM, allocating space in the memory pool to write to.
 * Return the destination address.
 */
static void *dynamic_dma_read(u8 *srcStart, u8 *srcEnd, UNUSED u32 side) {
    void *dest;
    u32 size = ALIGN16(srcEnd - srcStart);

#ifdef USE_SYSTEM_MALLOC
    dest = main_pool_alloc(size, NULL);
#else
    dest = main_pool_alloc(size, side);
#endif
    if (dest != NULL) {
        dma_read(dest, srcStart, srcEnd);
    }
    return dest;
}

#ifndef NO_SEGMENTED_MEMORY
/**
 * Load data from ROM into a newly allocated block, and set the segment base
 * address to this block.
 */
void *load_segment(s32 segment, u8 *srcStart, u8 *srcEnd, u32 side) {
    void *addr = dynamic_dma_read(srcStart, srcEnd, side);

    if (addr != NULL) {
        set_segment_base_addr(segment, addr);
    }
    return addr;
}

/*
 * Allocate a block of memory starting at destAddr and ending at the end of
 * the memory pool. Then copy srcStart through srcEnd from ROM to this block.
 * If this block is not large enough to hold the ROM data, or that portion
 * of the pool is already allocated, return NULL.
 */
void *load_to_fixed_pool_addr(u8 *destAddr, u8 *srcStart, u8 *srcEnd) {
    void *dest = NULL;
    u32 srcSize = ALIGN16(srcEnd - srcStart);
    u32 destSize = ALIGN16((u8 *) sPoolListHeadR - destAddr);

    if (srcSize <= destSize) {
        dest = main_pool_alloc(destSize, MEMORY_POOL_RIGHT);
        if (dest != NULL) {
            bzero(dest, destSize);
            osWritebackDCacheAll();
            dma_read(dest, srcStart, srcEnd);
            osInvalICache(dest, destSize);
            osInvalDCache(dest, destSize);
        }
    } else {
    }
    return dest;
}

/**
 * Decompress the block of ROM data from srcStart to srcEnd and return a
 * pointer to an allocated buffer holding the decompressed data. Set the
 * base address of segment to this address.
 */
void *load_segment_decompress(s32 segment, u8 *srcStart, u8 *srcEnd) {
    void *dest = NULL;

    u32 compSize = ALIGN16(srcEnd - srcStart);
    u8 *compressed = main_pool_alloc(compSize, MEMORY_POOL_RIGHT);

    // Decompressed size from mio0 header
    u32 *size = (u32 *) (compressed + 4);

    if (compressed != NULL) {
        dma_read(compressed, srcStart, srcEnd);
        dest = main_pool_alloc(*size, MEMORY_POOL_LEFT);
        if (dest != NULL) {
            decompress(compressed, dest);
            set_segment_base_addr(segment, dest);
            main_pool_free(compressed);
        } else {
        }
    } else {
    }
    return dest;
}

void *load_segment_decompress_heap(u32 segment, u8 *srcStart, u8 *srcEnd) {
    UNUSED void *dest = NULL;
    u32 compSize = ALIGN16(srcEnd - srcStart);
    u8 *compressed = main_pool_alloc(compSize, MEMORY_POOL_RIGHT);
    UNUSED u32 *pUncSize = (u32 *) (compressed + 4);

    if (compressed != NULL) {
        dma_read(compressed, srcStart, srcEnd);
        decompress(compressed, gDecompressionHeap);
        set_segment_base_addr(segment, gDecompressionHeap);
        main_pool_free(compressed);
    } else {
    }
    return gDecompressionHeap;
}

void load_engine_code_segment(void) {
    void *startAddr = (void *) SEG_ENGINE;
    u32 totalSize = SEG_FRAMEBUFFERS - SEG_ENGINE;
    UNUSED u32 alignedSize = ALIGN16(_engineSegmentRomEnd - _engineSegmentRomStart);

    bzero(startAddr, totalSize);
    osWritebackDCacheAll();
    dma_read(startAddr, _engineSegmentRomStart, _engineSegmentRomEnd);
    osInvalICache(startAddr, totalSize);
    osInvalDCache(startAddr, totalSize);
}
#endif

#ifdef USE_SYSTEM_MALLOC
static void alloc_only_pool_release_handler(void *addr) {
    struct AllocOnlyPool *pool = (struct AllocOnlyPool *) addr;
    struct AllocOnlyPoolBlock *block = pool->lastBlock;
    while (block != NULL) {
        struct AllocOnlyPoolBlock *prev = block->prev;
        free(block);
        block = prev;
    }
}

struct AllocOnlyPool *alloc_only_pool_init(void) {
    struct AllocOnlyPool *pool;
    void *addr = main_pool_alloc(sizeof(struct AllocOnlyPool), alloc_only_pool_release_handler);

    pool = (struct AllocOnlyPool *) addr;
    pool->lastBlock = NULL;
    pool->lastBlockSize = 0;
    pool->lastBlockNextPos = 0;

    return pool;
}

void alloc_only_pool_clear(struct AllocOnlyPool *pool) {
    alloc_only_pool_release_handler(pool);
    pool->lastBlock = NULL;
    pool->lastBlockSize = 0;
    pool->lastBlockNextPos = 0;
}

void *alloc_only_pool_alloc(struct AllocOnlyPool *pool, s32 size) {
    const size_t ptr_size = sizeof(u8 *);
    u32 s = size + ptr_size;
    if (pool->lastBlockSize - pool->lastBlockNextPos < s) {
        struct AllocOnlyPoolBlock *block;
        u32 nextSize = pool->lastBlockSize * 2;
        if (nextSize < 100) {
            nextSize = 100;
        }
        if (nextSize < s) {
            nextSize = s;
        }
        block = (struct AllocOnlyPoolBlock *) memalign(64, sizeof(struct AllocOnlyPoolBlock) + nextSize);
        if (block == NULL) {
            abort();
        }
        block->prev = pool->lastBlock;
        pool->lastBlock = block;
        pool->lastBlockSize = nextSize;
        pool->lastBlockNextPos = 0;
    }
    s -= ptr_size;
    uintptr_t addr = (uintptr_t) (pool->lastBlock + 1) + pool->lastBlockNextPos;
    uintptr_t addrAligned = ((addr - 1) | (ptr_size - 1)) + 1;
    s += addrAligned - addr;
    pool->lastBlockNextPos += s;
    return (u8 *)addrAligned;
}

struct MemoryPool *mem_pool_init(UNUSED u32 size, UNUSED u32 side) {
    struct MemoryPool *pool;
    void *addr = main_pool_alloc(sizeof(struct MemoryPool), NULL);
    u32 i;

    pool = (struct MemoryPool *) addr;
    pool->allocOnlyPool = alloc_only_pool_init();
    for (i = 0; i < ARRAY_COUNT(pool->bins); i++) {
        pool->bins[i] = NULL;
    }

    return pool;
}

void *mem_pool_alloc(struct MemoryPool *pool, u32 size) {
    struct FreeListNode *node;
    struct AllocatedNode *an;
    s32 bin = -1;
    u32 itemSize;
    u32 i;

    for (i = 3; i < 30; i++) {
        if (size <= (1U << i)) {
            bin = i;
            break;
        }
    }
    if (bin == -1) {
        abort();
    }
    itemSize = 1 << bin;
    node = pool->bins[bin - 3];
    if (node == NULL) {
        node = alloc_only_pool_alloc(pool->allocOnlyPool, sizeof(struct AllocatedNode) + itemSize);
        node->next = NULL;
        pool->bins[bin - 3] = node;
    }
    an = (struct AllocatedNode *) node;
    pool->bins[bin - 3] = node->next;
    an->bin = bin;
    return an + 1;
}

void mem_pool_free(struct MemoryPool *pool, void *addr) {
    struct AllocatedNode *an = ((struct AllocatedNode *) addr) - 1;
    struct FreeListNode *node = (struct FreeListNode *) an;
    s32 bin = an->bin;
    node->next = pool->bins[bin - 3];
    pool->bins[bin - 3] = node;
}

void *alloc_display_list(u32 size) {
    size = ALIGN8(size);
    return alloc_only_pool_alloc(gGfxAllocOnlyPool, size);
}
#else
/**
 * Allocate an allocation-only pool from the main pool. This pool doesn't
 * support freeing allocated memory.
 * Return NULL if there is not enough space in the main pool.
 */
struct AllocOnlyPool *alloc_only_pool_init(u32 size, u32 side) {
    void *addr;
    struct AllocOnlyPool *subPool = NULL;

    size = ALIGN4(size);
    addr = main_pool_alloc(size + sizeof(struct AllocOnlyPool), side);
    if (addr != NULL) {
        subPool = (struct AllocOnlyPool *) addr;
        subPool->totalSpace = size;
        subPool->usedSpace = 0;
        subPool->startPtr = (u8 *) addr + sizeof(struct AllocOnlyPool);
        subPool->freePtr = (u8 *) addr + sizeof(struct AllocOnlyPool);
    }
    return subPool;
}

/**
 * Allocate from an allocation-only pool.
 * Return NULL if there is not enough space.
 */
void *alloc_only_pool_alloc(struct AllocOnlyPool *pool, s32 size) {
    void *addr = NULL;

    size = ALIGN4(size);
    if (size > 0 && pool->usedSpace + size <= pool->totalSpace) {
        addr = pool->freePtr;
        pool->freePtr += size;
        pool->usedSpace += size;
    }
    return addr;
}

/**
 * Resize an allocation-only pool.
 * If the pool is increasing in size, the pool must be the last thing allocated
 * from the left end of the main pool.
 * The pool does not move.
 */
struct AllocOnlyPool *alloc_only_pool_resize(struct AllocOnlyPool *pool, u32 size) {
    struct AllocOnlyPool *newPool;

    size = ALIGN4(size);
    newPool = main_pool_realloc(pool, size + sizeof(struct AllocOnlyPool));
    if (newPool != NULL) {
        pool->totalSpace = size;
    }
    return newPool;
}

/**
 * Allocate a memory pool from the main pool. This pool supports arbitrary
 * order for allocation/freeing.
 * Return NULL if there is not enough space in the main pool.
 */
struct MemoryPool *mem_pool_init(u32 size, u32 side) {
    void *addr;
    struct MemoryBlock *block;
    struct MemoryPool *pool = NULL;

    size = ALIGN4(size);
    addr = main_pool_alloc(size + sizeof(struct MemoryPool), side);
    if (addr != NULL) {
        pool = (struct MemoryPool *) addr;

        pool->totalSpace = size;
        pool->firstBlock = (struct MemoryBlock *) ((u8 *) addr + sizeof(struct MemoryPool));
        pool->freeList.next = (struct MemoryBlock *) ((u8 *) addr + sizeof(struct MemoryPool));

        block = pool->firstBlock;
        block->next = NULL;
        block->size = pool->totalSpace;
    }
    return pool;
}

/**
 * Allocate from a memory pool. Return NULL if there is not enough space.
 */
void *mem_pool_alloc(struct MemoryPool *pool, u32 size) {
    struct MemoryBlock *freeBlock = &pool->freeList;
    void *addr = NULL;

    size = ALIGN4(size) + sizeof(struct MemoryBlock);
    while (freeBlock->next != NULL) {
        if (freeBlock->next->size >= size) {
            addr = (u8 *) freeBlock->next + sizeof(struct MemoryBlock);
            if (freeBlock->next->size - size <= sizeof(struct MemoryBlock)) {
                freeBlock->next = freeBlock->next->next;
            } else {
                struct MemoryBlock *newBlock = (struct MemoryBlock *) ((u8 *) freeBlock->next + size);
                newBlock->size = freeBlock->next->size - size;
                newBlock->next = freeBlock->next->next;
                freeBlock->next->size = size;
                freeBlock->next = newBlock;
            }
            break;
        }
        freeBlock = freeBlock->next;
    }
    return addr;
}

/**
 * Free a block that was allocated using mem_pool_alloc.
 */
void mem_pool_free(struct MemoryPool *pool, void *addr) {
    struct MemoryBlock *block = (struct MemoryBlock *) ((u8 *) addr - sizeof(struct MemoryBlock));
    struct MemoryBlock *freeList = pool->freeList.next;

    if (pool->freeList.next == NULL) {
        pool->freeList.next = block;
        block->next = NULL;
    } else {
        if (block < pool->freeList.next) {
            if ((u8 *) pool->freeList.next == (u8 *) block + block->size) {
                block->size += freeList->size;
                block->next = freeList->next;
                pool->freeList.next = block;
            } else {
                block->next = pool->freeList.next;
                pool->freeList.next = block;
            }
        } else {
            while (freeList->next != NULL) {
                if (freeList < block && block < freeList->next) {
                    break;
                }
                freeList = freeList->next;
            }
            if ((u8 *) freeList + freeList->size == (u8 *) block) {
                freeList->size += block->size;
                block = freeList;
            } else {
                block->next = freeList->next;
                freeList->next = block;
            }
            if (block->next != NULL && (u8 *) block->next == (u8 *) block + block->size) {
                block->size = block->size + block->next->size;
                block->next = block->next->next;
            }
        }
    }
}

void *alloc_display_list(u32 size) {
    void *ptr = NULL;

    size = ALIGN8(size);
    if (gGfxPoolEnd - size >= (u8 *) gDisplayListHead) {
        gGfxPoolEnd -= size;
        ptr = gGfxPoolEnd;
    } else {
    }
    return ptr;
}
#endif

static struct DmaTable *load_dma_table_address(u8 *srcAddr) {
    struct DmaTable *table = dynamic_dma_read(srcAddr, srcAddr + sizeof(u32),
                                                             MEMORY_POOL_LEFT);
    u32 size = table->count * sizeof(struct OffsetSizePair) + 
        sizeof(struct DmaTable) - sizeof(struct OffsetSizePair);
    main_pool_free(table);

    table = dynamic_dma_read(srcAddr, srcAddr + size, MEMORY_POOL_LEFT);
    table->srcAddr = srcAddr;
    return table;
}

void setup_dma_table_list(struct DmaHandlerList *list, void *srcAddr, void *buffer) {
    if (srcAddr != NULL) {
        list->dmaTable = load_dma_table_address(srcAddr);
    }
    list->currentAddr = NULL;
    list->bufTarget = buffer;
}

s32 load_patchable_table(struct DmaHandlerList *list, s32 index) {
    s32 ret = FALSE;
    struct DmaTable *table = list->dmaTable;

    if ((u32)index < table->count) {
        u8 *addr = table->srcAddr + table->anim[index].offset;
        s32 size = table->anim[index].size;

        if (list->currentAddr != addr) {
            dma_read(list->bufTarget, addr, addr + size);
            list->currentAddr = addr;
            ret = TRUE;
        }
    }
    return ret;
}

#ifdef TARGET_WII_U
#include <coreinit/cache.h>
#include <coreinit/memory.h>
#include <dmae/mem.h>

void *memcpy(void *_dst, const void *_src, u32 size) {
    if (size > 5120) {
        const u32 srcMisalignment = (uintptr_t)_src & 7U;
        const u32 dstMisalignment = (uintptr_t)_dst & 7U;

        if (srcMisalignment == dstMisalignment) {
            u8       *dst = (u8       *)_dst;
            const u8 *src = (const u8 *)_src;

            if (srcMisalignment) {
                const u32 misalignmentInv = 8U - srcMisalignment;
                OSBlockMove(dst, src, misalignmentInv, FALSE);
                src += misalignmentInv;
                dst += misalignmentInv;
            }

            const u32 sizeAligned   = size & ~3U;
            const u32 sizeRemainder = size & 3U;

            DCFlushRange((void *)src, sizeAligned);
            DCFlushRange((void *)dst, sizeAligned);
            while (!DMAEWaitDone(DMAECopyMem(dst, src, sizeAligned >> 2, DMAE_SWAP_NONE))) {
            }

            if (sizeRemainder) {
                OSBlockMove(dst + sizeAligned, src + sizeAligned, sizeRemainder, FALSE);
            }

            DCFlushRange(_dst, size);
            return _dst;
        }
    }

    return OSBlockMove(_dst, _src, size, FALSE);
}

void *memmove(void *dst, const void *src, u32 size) {
    if (src + size < dst || dst + size < src) {
        return memcpy(dst, src, size);
    }

    return OSBlockMove(dst, src, size, FALSE);
}

void *memset(void *_dst, int val, u32 size) {
    if (size > 5120) {
        const u32 dstMisalignment = (uintptr_t)_dst & 7U;
        u8 *dst = (u8 *)_dst;

        if (dstMisalignment) {
            const u32 misalignmentInv = 8U - dstMisalignment;
            OSBlockSet(dst, val, misalignmentInv);
            dst += misalignmentInv;
        }

        const u32 sizeAligned   = size & ~3U;
        const u32 sizeRemainder = size & 3U;

        DCFlushRange((void *)dst, sizeAligned);
        while (!DMAEWaitDone(DMAEFillMem(dst, val, sizeAligned >> 2))) {
        }

        if (sizeRemainder) {
            OSBlockSet(dst + sizeAligned, val, sizeRemainder);
        }

        DCFlushRange(_dst, size);
        return _dst;
    }

    return OSBlockSet(_dst, val, size);
}

#endif
