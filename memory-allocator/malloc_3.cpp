#include <unistd.h>
#include <sys/mman.h>
#include <cstddef>
#include <cstring>

/* ============================================================
 * Constants
 * ============================================================ */
#define MAX_ALLOC_SIZE   (1e8)
#define MAX_ORDER        10
#define MIN_BLOCK_SIZE   128                          /* bytes, order 0 */
#define INITIAL_BLOCKS   32
#define MMAP_THRESHOLD   (128 * 1024)                /* 128 KB */
#define TOTAL_HEAP_SIZE  (INITIAL_BLOCKS * MMAP_THRESHOLD)  /* 32 * 128KB */

/* ============================================================
 * Metadata structure
 * sits at the VERY START of every block (buddy or mmap).
 * size = total block size including this struct.
 * ============================================================ */
struct MallocMetadata {
    size_t          size;      /* total block size (metadata + usable) */
    bool            is_free;
    bool            is_mmap;
    MallocMetadata *next;
    MallocMetadata *prev;
};

static_assert(sizeof(MallocMetadata) <= 64, "metadata must be <= 64 bytes");

/* ============================================================
 * Global state
 * ============================================================ */

/* free_lists[i] = doubly-linked list of all free blocks of order i,
 * sorted by memory address ascending */
static MallocMetadata* free_lists[MAX_ORDER + 1];

/* mmap list (unsorted, for large allocations) */
static MallocMetadata* mmap_list = nullptr;

/* Base address of the 32 initial 128-KB blocks */
static void*  heap_base   = nullptr;
static bool   initialized = false;

/* ============================================================
 * Helper: order ↔ block size
 * block_size(order) = MIN_BLOCK_SIZE << order
 * ============================================================ */
static inline size_t block_size(int order) {
    return (size_t)MIN_BLOCK_SIZE << order;
}

static int size_to_order(size_t usable_size) {
    size_t total_needed = usable_size + sizeof(MallocMetadata);
    int order = 0;
    size_t bsz = MIN_BLOCK_SIZE;
    while (bsz < total_needed && order < MAX_ORDER) {
        order++;
        bsz <<= 1;
    }
    return order;  /* block_size(order) >= total_needed */
}

/* ============================================================
 * Free-list operations (sorted by address)
 * ============================================================ */
static void fl_insert(int order, MallocMetadata* m) {
    m->is_free = true;
    m->next    = nullptr;
    m->prev    = nullptr;

    MallocMetadata** head = &free_lists[order];
    if (*head == nullptr || m < *head) {
        m->next = *head;
        if (*head) (*head)->prev = m;
        *head = m;
        return;
    }
    MallocMetadata* cur = *head;
    while (cur->next && cur->next < m)
        cur = cur->next;
    m->next = cur->next;
    m->prev = cur;
    if (cur->next) cur->next->prev = m;
    cur->next = m;
}

static void fl_remove(int order, MallocMetadata* m) {
    MallocMetadata** head = &free_lists[order];
    if (m->prev) m->prev->next = m->next;
    else         *head = m->next;
    if (m->next) m->next->prev = m->prev;
    m->prev = nullptr;
    m->next = nullptr;
}

/* ============================================================
 * Buddy address computation (XOR trick — works because
 * heap_base is aligned to TOTAL_HEAP_SIZE = 32 * 128KB,
 * and all blocks have power-of-two sizes).
 * ============================================================ */
static MallocMetadata* get_buddy(MallocMetadata* m, int order) {
    size_t bsz    = block_size(order);
    size_t offset = (size_t)((char*)m - (char*)heap_base);
    size_t buddy_offset = offset ^ bsz;
    return (MallocMetadata*)((char*)heap_base + buddy_offset);
}

static int get_order(MallocMetadata* m) {
    size_t bsz = m->size;
    int order = 0;
    size_t s = MIN_BLOCK_SIZE;
    while (s < bsz && order < MAX_ORDER) { order++; s <<= 1; }
    return order;
}

/* ============================================================
 * Initialization: allocate 32 blocks of 128KB, aligned.
 * ============================================================ */
static void buddy_init() {
    if (initialized) return;
    initialized = true;

    /* Align current program break to TOTAL_HEAP_SIZE boundary */
    void* cur = sbrk(0);
    size_t align_size = TOTAL_HEAP_SIZE;
    size_t cur_addr   = (size_t)cur;
    size_t remainder  = cur_addr % align_size;
    if (remainder != 0) {
        size_t pad = align_size - remainder;
        if (sbrk((intptr_t)pad) == (void*)(-1)) return; /* fatal */
    }

    /* Now allocate the 32 blocks */
    heap_base = sbrk((intptr_t)TOTAL_HEAP_SIZE);
    if (heap_base == (void*)(-1)) return;

    /* Initialize 32 free blocks of MAX_ORDER */
    for (int i = 0; i < INITIAL_BLOCKS; i++) {
        MallocMetadata* m = (MallocMetadata*)((char*)heap_base + i * MMAP_THRESHOLD);
        m->size    = MMAP_THRESHOLD;
        m->is_free = true;
        m->is_mmap = false;
        m->next    = nullptr;
        m->prev    = nullptr;
        fl_insert(MAX_ORDER, m);
    }
}

/* ============================================================
 * Split a block at 'order' in half; returns the lower half.
 * The upper half is placed in free_lists[order-1].
 * ============================================================ */
static MallocMetadata* split_block(MallocMetadata* m, int order) {
    /* Remove m from free_lists[order] */
    fl_remove(order, m);

    int new_order    = order - 1;
    size_t new_size  = block_size(new_order);

    /* Lower half */
    m->size    = new_size;
    m->is_free = false;

    /* Upper half (buddy) */
    MallocMetadata* buddy = (MallocMetadata*)((char*)m + new_size);
    buddy->size    = new_size;
    buddy->is_free = true;
    buddy->is_mmap = false;
    buddy->next    = nullptr;
    buddy->prev    = nullptr;
    fl_insert(new_order, buddy);

    return m;
}

/* ============================================================
 * smalloc core: find/split a free block of the right order
 * ============================================================ */
static MallocMetadata* buddy_alloc(size_t usable_size) {
    int target_order = size_to_order(usable_size);
    if (target_order > MAX_ORDER) return nullptr;

    /* Find smallest sufficient free block */
    for (int ord = target_order; ord <= MAX_ORDER; ord++) {
        if (free_lists[ord] == nullptr) continue;

        MallocMetadata* m = free_lists[ord];
        /* Split down to target_order */
        while (ord > target_order) {
            m = split_block(m, ord);
            ord--;
        }
        /* Now m is at target_order */
        fl_remove(target_order, m);
        m->is_free = false;
        return m;
    }
    return nullptr; /* no free block found */
}

/* ============================================================
 * Merge (coalesce) buddies after free
 * ============================================================ */
static void buddy_free_and_merge(MallocMetadata* m) {
    int order = get_order(m);

    while (order < MAX_ORDER) {
        MallocMetadata* buddy = get_buddy(m, order);

        /* Buddy must be inside our heap, free, and same order */
        if ((char*)buddy < (char*)heap_base ||
            (char*)buddy >= (char*)heap_base + TOTAL_HEAP_SIZE)
            break;
        if (!buddy->is_free || buddy->size != block_size(order))
            break;

        /* Merge: remove buddy from its free list */
        fl_remove(order, buddy);

        /* The lower-address block becomes the merged block */
        if (buddy < m) m = buddy;
        order++;
        m->size = block_size(order);
    }

    m->is_free = true;
    fl_insert(order, m);
}

/* ============================================================
 * mmap helpers
 * ============================================================ */
static MallocMetadata* mmap_alloc(size_t usable_size) {
    size_t total = sizeof(MallocMetadata) + usable_size;
    /* Round up to page size */
    size_t page  = (size_t)sysconf(_SC_PAGESIZE);
    total = (total + page - 1) & ~(page - 1);

    void* raw = mmap(nullptr, total, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (raw == MAP_FAILED) return nullptr;

    MallocMetadata* m = (MallocMetadata*)raw;
    m->size    = total - sizeof(MallocMetadata);
    m->is_free = false;
    m->is_mmap = true;
    m->next    = mmap_list;
    m->prev    = nullptr;
    if (mmap_list) mmap_list->prev = m;
    mmap_list = m;

    return m;
}

static void mmap_free(MallocMetadata* m) {
    /* Remove from mmap_list */
    if (m->prev) m->prev->next = m->next;
    else         mmap_list = m->next;
    if (m->next) m->next->prev = m->prev;

    size_t total = sizeof(MallocMetadata) + m->size;
    munmap(m, total);
}

/* ============================================================
 * User pointer helpers
 * ============================================================ */
static inline MallocMetadata* meta_from_ptr(void* p) {
    return (MallocMetadata*)((char*)p - sizeof(MallocMetadata));
}
static inline void* ptr_from_meta(MallocMetadata* m) {
    return (void*)((char*)m + sizeof(MallocMetadata));
}

/* ============================================================
 * Public API
 * ============================================================ */

void* smalloc(size_t size) {
    if (size == 0 || (double)size > MAX_ALLOC_SIZE)
        return nullptr;

    buddy_init();

    /* Large allocation: use mmap */
    if (size >= MMAP_THRESHOLD) {
        MallocMetadata* m = mmap_alloc(size);
        if (!m) return nullptr;
        return ptr_from_meta(m);
    }

    /* Buddy allocation */
    MallocMetadata* m = buddy_alloc(size);
    if (!m) return nullptr;
    return ptr_from_meta(m);
}

void* scalloc(size_t num, size_t size) {
    if (num == 0 || size == 0) return nullptr;
    size_t total = num * size;
    if ((double)total > MAX_ALLOC_SIZE) return nullptr;

    void* ptr = smalloc(total);
    if (!ptr) return nullptr;
    std::memset(ptr, 0, total);
    return ptr;
}

void sfree(void* p) {
    if (!p) return;

    MallocMetadata* m = meta_from_ptr(p);
    if (m->is_free) return;

    if (m->is_mmap) {
        mmap_free(m);
        return;
    }

    buddy_free_and_merge(m);
}

void* srealloc(void* oldp, size_t size) {
    if (size == 0 || (double)size > MAX_ALLOC_SIZE)
        return nullptr;
    if (!oldp)
        return smalloc(size);

    MallocMetadata* m = meta_from_ptr(oldp);

    /* mmap block: only reuse if same size */
    if (m->is_mmap) {
        if (m->size >= size) return oldp;
        void* newp = smalloc(size);
        if (!newp) return nullptr;
        std::memmove(newp, oldp, m->size);
        mmap_free(m);
        return newp;
    }

    /* --- Buddy block --- */

    /* (a) Current block already fits */
    if (m->size - sizeof(MallocMetadata) >= size &&
        m->size >= sizeof(MallocMetadata) + size) {
        return oldp;
    }
    /* More precisely: usable bytes = m->size - sizeof(MallocMetadata) */
    size_t usable = m->size - sizeof(MallocMetadata);
    if (usable >= size) return oldp;

    /* (b) Try iterative merging with buddy until big enough */
    int orig_order = get_order(m);
    {
        /* Simulate merging to find if we can satisfy the request */
        MallocMetadata* cur = m;
        int ord = orig_order;
        while (ord < MAX_ORDER) {
            MallocMetadata* buddy = get_buddy(cur, ord);
            if ((char*)buddy < (char*)heap_base ||
                (char*)buddy >= (char*)heap_base + TOTAL_HEAP_SIZE)
                break;
            if (!buddy->is_free || buddy->size != block_size(ord))
                break;
            /* merge */
            if (buddy < cur) cur = buddy;
            ord++;
            size_t new_usable = block_size(ord) - sizeof(MallocMetadata);
            if (new_usable >= size) {
                /* Do the actual merge */
                /* First, mark m as free to let buddy_free_and_merge do it */
                m->is_free = true;
                /* Remove m from block perspective and merge */
                /* We re-do this properly: */
                MallocMetadata* merged = m;
                int merge_ord = orig_order;
                while (merge_ord < ord) {
                    MallocMetadata* b = get_buddy(merged, merge_ord);
                    fl_remove(merge_ord, b);
                    if (b < merged) merged = b;
                    merge_ord++;
                    merged->size = block_size(merge_ord);
                }
                merged->is_free = false;
                /* copy data if merged base changed */
                if (merged != m)
                    std::memmove(ptr_from_meta(merged), oldp,
                                 (usable < size) ? usable : size);
                return ptr_from_meta(merged);
            }
        }
    }

    /* (c) Find another block; free current first (with merging) */
    void* newp = smalloc(size);
    if (!newp) return nullptr;
    std::memmove(newp, oldp, usable);
    /* Free old block */
    m->is_free = false; /* ensure sfree logic works */
    sfree(oldp);
    return newp;
}

/* ============================================================
 * Stats — only buddy blocks (not mmap)
 * ============================================================ */

size_t _num_free_blocks() {
    size_t count = 0;
    for (int ord = 0; ord <= MAX_ORDER; ord++) {
        for (MallocMetadata* m = free_lists[ord]; m; m = m->next)
            count++;
    }
    return count;
}

size_t _num_free_bytes() {
    size_t bytes = 0;
    for (int ord = 0; ord <= MAX_ORDER; ord++) {
        for (MallocMetadata* m = free_lists[ord]; m; m = m->next)
            bytes += m->size - sizeof(MallocMetadata);
    }
    return bytes;
}

/* Count all buddy blocks (free + used) in the initial heap */
static void count_all_buddy(size_t* blocks, size_t* bytes) {
    *blocks = 0;
    *bytes  = 0;
    if (!heap_base) return;

    /* Walk every possible block by reconstructing the layout.
     * We do this by scanning in steps of MIN_BLOCK_SIZE within
     * heap_base + TOTAL_HEAP_SIZE, reading each block's size. */
    char* ptr = (char*)heap_base;
    char* end = ptr + TOTAL_HEAP_SIZE;
    while (ptr < end) {
        MallocMetadata* m = (MallocMetadata*)ptr;
        if (m->size == 0) break; /* safety */
        (*blocks)++;
        (*bytes) += m->size - sizeof(MallocMetadata);
        ptr += m->size;
    }
}

size_t _num_allocated_blocks() {
    size_t blocks = 0, bytes = 0;
    count_all_buddy(&blocks, &bytes);
    /* Add mmap blocks */
    for (MallocMetadata* m = mmap_list; m; m = m->next)
        blocks++;
    return blocks;
}

size_t _num_allocated_bytes() {
    size_t blocks = 0, bytes = 0;
    count_all_buddy(&blocks, &bytes);
    for (MallocMetadata* m = mmap_list; m; m = m->next)
        bytes += m->size;
    return bytes;
}

size_t _num_meta_data_bytes() {
    return _num_allocated_blocks() * sizeof(MallocMetadata);
}

size_t _size_meta_data() {
    return sizeof(MallocMetadata);
}
