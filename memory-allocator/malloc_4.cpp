/* malloc_4.cpp — Part 4 (optional): extends malloc_3 with HugePage support.
 *
 * Additional rule:
 *   - smalloc(size >= 4MB): use mmap with MAP_HUGETLB
 *   - scalloc where ONE element's size > 2MB: use mmap with MAP_HUGETLB
 *   - Everything else: same as malloc_3.
 *
 * We add a field 'is_huge' to the metadata to distinguish hugepage blocks.
 * MAP_HUGETLB requires kernel support; if it fails, we fall back to
 * regular mmap.
 */

#include <unistd.h>
#include <sys/mman.h>
#include <cstddef>
#include <cstring>

#define MAX_ALLOC_SIZE    (1e8)
#define MAX_ORDER         10
#define MIN_BLOCK_SIZE    128
#define INITIAL_BLOCKS    32
#define MMAP_THRESHOLD    (128 * 1024)
#define TOTAL_HEAP_SIZE   (INITIAL_BLOCKS * MMAP_THRESHOLD)
#define HUGEPAGE_SMALLOC  (4 * 1024 * 1024)   /* 4 MB */
#define HUGEPAGE_SCALLOC  (2 * 1024 * 1024)   /* 2 MB per element */
#define HUGEPAGE_SIZE     (2 * 1024 * 1024)   /* standard huge page */

struct MallocMetadata {
    size_t          size;
    bool            is_free;
    bool            is_mmap;
    bool            is_huge;
    MallocMetadata *next;
    MallocMetadata *prev;
};

static_assert(sizeof(MallocMetadata) <= 64, "metadata must be <= 64 bytes");

static MallocMetadata* free_lists[MAX_ORDER + 1];
static MallocMetadata* mmap_list  = nullptr;
static void*           heap_base  = nullptr;
static bool            initialized = false;

/* ============================================================ helpers */
static inline size_t block_size(int order) {
    return (size_t)MIN_BLOCK_SIZE << order;
}
static int size_to_order(size_t usable_size) {
    size_t needed = usable_size + sizeof(MallocMetadata);
    int order = 0;
    size_t bsz = MIN_BLOCK_SIZE;
    while (bsz < needed && order < MAX_ORDER) { order++; bsz <<= 1; }
    return order;
}
static inline MallocMetadata* meta_from_ptr(void* p) {
    return (MallocMetadata*)((char*)p - sizeof(MallocMetadata));
}
static inline void* ptr_from_meta(MallocMetadata* m) {
    return (void*)((char*)m + sizeof(MallocMetadata));
}

/* ============================================================ free-list */
static void fl_insert(int order, MallocMetadata* m) {
    m->is_free = true; m->next = nullptr; m->prev = nullptr;
    MallocMetadata** head = &free_lists[order];
    if (!*head || m < *head) {
        m->next = *head;
        if (*head) (*head)->prev = m;
        *head = m; return;
    }
    MallocMetadata* cur = *head;
    while (cur->next && cur->next < m) cur = cur->next;
    m->next = cur->next; m->prev = cur;
    if (cur->next) cur->next->prev = m;
    cur->next = m;
}
static void fl_remove(int order, MallocMetadata* m) {
    MallocMetadata** head = &free_lists[order];
    if (m->prev) m->prev->next = m->next; else *head = m->next;
    if (m->next) m->next->prev = m->prev;
    m->prev = nullptr; m->next = nullptr;
}
static MallocMetadata* get_buddy(MallocMetadata* m, int order) {
    size_t bsz = block_size(order);
    size_t offset = (size_t)((char*)m - (char*)heap_base);
    return (MallocMetadata*)((char*)heap_base + (offset ^ bsz));
}
static int get_order(MallocMetadata* m) {
    int order = 0; size_t s = MIN_BLOCK_SIZE;
    while (s < m->size && order < MAX_ORDER) { order++; s <<= 1; }
    return order;
}

/* ============================================================ init */
static void buddy_init() {
    if (initialized) return;
    initialized = true;
    void* cur = sbrk(0);
    size_t cur_addr = (size_t)cur;
    size_t remainder = cur_addr % TOTAL_HEAP_SIZE;
    if (remainder) sbrk((intptr_t)(TOTAL_HEAP_SIZE - remainder));
    heap_base = sbrk((intptr_t)TOTAL_HEAP_SIZE);
    if (heap_base == (void*)(-1)) return;
    for (int i = 0; i < INITIAL_BLOCKS; i++) {
        MallocMetadata* m = (MallocMetadata*)((char*)heap_base + i * MMAP_THRESHOLD);
        m->size = MMAP_THRESHOLD; m->is_free = true;
        m->is_mmap = false; m->is_huge = false;
        m->next = nullptr; m->prev = nullptr;
        fl_insert(MAX_ORDER, m);
    }
}

/* ============================================================ split/merge */
static MallocMetadata* split_block(MallocMetadata* m, int order) {
    fl_remove(order, m);
    int new_order = order - 1; size_t new_size = block_size(new_order);
    m->size = new_size; m->is_free = false;
    MallocMetadata* buddy = (MallocMetadata*)((char*)m + new_size);
    buddy->size = new_size; buddy->is_free = true;
    buddy->is_mmap = false; buddy->is_huge = false;
    buddy->next = nullptr; buddy->prev = nullptr;
    fl_insert(new_order, buddy);
    return m;
}
static MallocMetadata* buddy_alloc(size_t usable_size) {
    int target = size_to_order(usable_size);
    if (target > MAX_ORDER) return nullptr;
    for (int ord = target; ord <= MAX_ORDER; ord++) {
        if (!free_lists[ord]) continue;
        MallocMetadata* m = free_lists[ord];
        while (ord > target) { m = split_block(m, ord); ord--; }
        fl_remove(target, m); m->is_free = false; return m;
    }
    return nullptr;
}
static void buddy_free_and_merge(MallocMetadata* m) {
    int order = get_order(m);
    while (order < MAX_ORDER) {
        MallocMetadata* buddy = get_buddy(m, order);
        if ((char*)buddy < (char*)heap_base ||
            (char*)buddy >= (char*)heap_base + TOTAL_HEAP_SIZE) break;
        if (!buddy->is_free || buddy->size != block_size(order)) break;
        fl_remove(order, buddy);
        if (buddy < m) m = buddy;
        order++; m->size = block_size(order);
    }
    m->is_free = true; fl_insert(order, m);
}

/* ============================================================ mmap */
static MallocMetadata* mmap_alloc_internal(size_t usable_size, bool hugepage) {
    size_t total = sizeof(MallocMetadata) + usable_size;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    size_t align = (size_t)sysconf(_SC_PAGESIZE);

    if (hugepage) {
        align = HUGEPAGE_SIZE;
        /* Try with hugetlb; fall back if not supported */
        void* raw = mmap(nullptr, (total + HUGEPAGE_SIZE - 1) & ~(size_t)(HUGEPAGE_SIZE - 1),
                         PROT_READ | PROT_WRITE,
                         flags | MAP_HUGETLB, -1, 0);
        if (raw != MAP_FAILED) {
            size_t rounded = (total + HUGEPAGE_SIZE - 1) & ~(size_t)(HUGEPAGE_SIZE - 1);
            MallocMetadata* m = (MallocMetadata*)raw;
            m->size = rounded - sizeof(MallocMetadata);
            m->is_free = false; m->is_mmap = true; m->is_huge = true;
            m->next = mmap_list; m->prev = nullptr;
            if (mmap_list) mmap_list->prev = m;
            mmap_list = m;
            return m;
        }
        /* fallthrough to regular mmap */
    }

    total = (total + align - 1) & ~(align - 1);
    void* raw = mmap(nullptr, total, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (raw == MAP_FAILED) return nullptr;
    MallocMetadata* m = (MallocMetadata*)raw;
    m->size = total - sizeof(MallocMetadata);
    m->is_free = false; m->is_mmap = true; m->is_huge = false;
    m->next = mmap_list; m->prev = nullptr;
    if (mmap_list) mmap_list->prev = m;
    mmap_list = m;
    return m;
}

static void mmap_free(MallocMetadata* m) {
    if (m->prev) m->prev->next = m->next; else mmap_list = m->next;
    if (m->next) m->next->prev = m->prev;
    munmap(m, sizeof(MallocMetadata) + m->size);
}

/* ============================================================ public API */
void* smalloc(size_t size) {
    if (size == 0 || (double)size > MAX_ALLOC_SIZE) return nullptr;
    buddy_init();

    if (size >= MMAP_THRESHOLD) {
        bool huge = (size >= HUGEPAGE_SMALLOC);
        MallocMetadata* m = mmap_alloc_internal(size, huge);
        return m ? ptr_from_meta(m) : nullptr;
    }
    MallocMetadata* m = buddy_alloc(size);
    return m ? ptr_from_meta(m) : nullptr;
}

void* scalloc(size_t num, size_t size) {
    if (!num || !size) return nullptr;
    size_t total = num * size;
    if ((double)total > MAX_ALLOC_SIZE) return nullptr;
    buddy_init();

    void* ptr;
    if (total >= MMAP_THRESHOLD) {
        bool huge = (size > HUGEPAGE_SCALLOC);
        MallocMetadata* m = mmap_alloc_internal(total, huge);
        if (!m) return nullptr;
        ptr = ptr_from_meta(m);
    } else {
        MallocMetadata* m = buddy_alloc(total);
        if (!m) return nullptr;
        ptr = ptr_from_meta(m);
    }
    std::memset(ptr, 0, total);
    return ptr;
}

void sfree(void* p) {
    if (!p) return;
    MallocMetadata* m = meta_from_ptr(p);
    if (m->is_free) return;
    if (m->is_mmap) { mmap_free(m); return; }
    buddy_free_and_merge(m);
}

void* srealloc(void* oldp, size_t size) {
    if (!size || (double)size > MAX_ALLOC_SIZE) return nullptr;
    if (!oldp) return smalloc(size);

    MallocMetadata* m = meta_from_ptr(oldp);

    if (m->is_mmap) {
        if (m->size >= size) return oldp;
        void* newp = smalloc(size);
        if (!newp) return nullptr;
        std::memmove(newp, oldp, m->size);
        mmap_free(m);
        return newp;
    }

    size_t usable = m->size - sizeof(MallocMetadata);
    if (usable >= size) return oldp;

    int orig_order = get_order(m);
    MallocMetadata* cur = m; int ord = orig_order;
    while (ord < MAX_ORDER) {
        MallocMetadata* buddy = get_buddy(cur, ord);
        if ((char*)buddy < (char*)heap_base ||
            (char*)buddy >= (char*)heap_base + TOTAL_HEAP_SIZE) break;
        if (!buddy->is_free || buddy->size != block_size(ord)) break;
        if (buddy < cur) cur = buddy;
        ord++;
        if (block_size(ord) - sizeof(MallocMetadata) >= size) {
            MallocMetadata* merged = m; int mo = orig_order;
            while (mo < ord) {
                MallocMetadata* b = get_buddy(merged, mo);
                fl_remove(mo, b);
                if (b < merged) merged = b;
                mo++; merged->size = block_size(mo);
            }
            merged->is_free = false;
            if (merged != m)
                std::memmove(ptr_from_meta(merged), oldp, usable);
            return ptr_from_meta(merged);
        }
    }

    void* newp = smalloc(size);
    if (!newp) return nullptr;
    std::memmove(newp, oldp, usable);
    sfree(oldp);
    return newp;
}

/* ============================================================ stats */
size_t _num_free_blocks() {
    size_t c = 0;
    for (int o = 0; o <= MAX_ORDER; o++)
        for (MallocMetadata* m = free_lists[o]; m; m = m->next) c++;
    return c;
}
size_t _num_free_bytes() {
    size_t b = 0;
    for (int o = 0; o <= MAX_ORDER; o++)
        for (MallocMetadata* m = free_lists[o]; m; m = m->next)
            b += m->size - sizeof(MallocMetadata);
    return b;
}
static void count_all_buddy(size_t* blocks, size_t* bytes) {
    *blocks = *bytes = 0;
    if (!heap_base) return;
    char* ptr = (char*)heap_base, *end = ptr + TOTAL_HEAP_SIZE;
    while (ptr < end) {
        MallocMetadata* m = (MallocMetadata*)ptr;
        if (!m->size) break;
        (*blocks)++; (*bytes) += m->size - sizeof(MallocMetadata);
        ptr += m->size;
    }
}
size_t _num_allocated_blocks() {
    size_t b = 0, by = 0; count_all_buddy(&b, &by);
    for (MallocMetadata* m = mmap_list; m; m = m->next) b++;
    return b;
}
size_t _num_allocated_bytes() {
    size_t b = 0, by = 0; count_all_buddy(&b, &by);
    for (MallocMetadata* m = mmap_list; m; m = m->next) by += m->size;
    return by;
}
size_t _num_meta_data_bytes() {
    return _num_allocated_blocks() * sizeof(MallocMetadata);
}
size_t _size_meta_data() {
    return sizeof(MallocMetadata);
}
