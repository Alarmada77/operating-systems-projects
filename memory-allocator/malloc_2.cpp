#include <unistd.h>
#include <cstddef>
#include <cstring>

#define MAX_ALLOC_SIZE (1e8)

/* ============================================================
 * Metadata structure — lives just before the usable block.
 * The linked list is sorted by memory address (ascending).
 * ============================================================ */
struct MallocMetadata {
    size_t         size;    /* usable bytes (not counting this struct) */
    bool           is_free;
    MallocMetadata *next;
    MallocMetadata *prev;
};

/* Head of the global list (sorted by address) */
static MallocMetadata *g_head = nullptr;

/* ============================================================
 * Internal helpers
 * ============================================================ */

/* Return metadata pointer from user pointer */
static inline MallocMetadata* meta_from_ptr(void* p) {
    return (MallocMetadata*)((char*)p - sizeof(MallocMetadata));
}

/* Return user pointer from metadata pointer */
static inline void* ptr_from_meta(MallocMetadata* m) {
    return (void*)((char*)m + sizeof(MallocMetadata));
}

/* Find the first free block with at least 'size' usable bytes (ascending) */
static MallocMetadata* find_free(size_t size) {
    for (MallocMetadata* m = g_head; m != nullptr; m = m->next) {
        if (m->is_free && m->size >= size)
            return m;
    }
    return nullptr;
}

/* Append a new block at the end of the heap via sbrk */
static MallocMetadata* alloc_new_block(size_t size) {
    size_t total = sizeof(MallocMetadata) + size;
    void* raw = sbrk((intptr_t)total);
    if (raw == (void*)(-1))
        return nullptr;

    MallocMetadata* m = (MallocMetadata*)raw;
    m->size    = size;
    m->is_free = false;
    m->next    = nullptr;
    m->prev    = nullptr;

    /* Append to tail of sorted list */
    if (g_head == nullptr) {
        g_head = m;
    } else {
        MallocMetadata* tail = g_head;
        while (tail->next) tail = tail->next;
        tail->next = m;
        m->prev    = tail;
    }

    return m;
}

/* ============================================================
 * Public API
 * ============================================================ */

void* smalloc(size_t size) {
    if (size == 0 || (double)size > MAX_ALLOC_SIZE)
        return nullptr;

    /* Try to reuse a free block */
    MallocMetadata* m = find_free(size);
    if (m != nullptr) {
        m->is_free = false;
        return ptr_from_meta(m);
    }

    /* Allocate a new block from the heap */
    m = alloc_new_block(size);
    if (m == nullptr)
        return nullptr;

    return ptr_from_meta(m);
}

void* scalloc(size_t num, size_t size) {
    if (num == 0 || size == 0)
        return nullptr;

    size_t total = num * size;
    if ((double)total > MAX_ALLOC_SIZE)
        return nullptr;

    void* ptr = smalloc(total);
    if (ptr == nullptr)
        return nullptr;

    std::memset(ptr, 0, total);
    return ptr;
}

void sfree(void* p) {
    if (p == nullptr)
        return;

    MallocMetadata* m = meta_from_ptr(p);
    if (m->is_free)
        return;

    m->is_free = true;
}

void* srealloc(void* oldp, size_t size) {
    if (size == 0 || (double)size > MAX_ALLOC_SIZE)
        return nullptr;

    if (oldp == nullptr)
        return smalloc(size);

    MallocMetadata* m = meta_from_ptr(oldp);

    /* Block is large enough — reuse it */
    if (m->size >= size)
        return oldp;

    /* Need a bigger block */
    void* newp = smalloc(size);
    if (newp == nullptr)
        return nullptr;

    std::memmove(newp, oldp, m->size);
    sfree(oldp);
    return newp;
}

/* ============================================================
 * Stats
 * ============================================================ */

size_t _num_free_blocks() {
    size_t count = 0;
    for (MallocMetadata* m = g_head; m; m = m->next)
        if (m->is_free) count++;
    return count;
}

size_t _num_free_bytes() {
    size_t bytes = 0;
    for (MallocMetadata* m = g_head; m; m = m->next)
        if (m->is_free) bytes += m->size;
    return bytes;
}

size_t _num_allocated_blocks() {
    size_t count = 0;
    for (MallocMetadata* m = g_head; m; m = m->next)
        count++;
    return count;
}

size_t _num_allocated_bytes() {
    size_t bytes = 0;
    for (MallocMetadata* m = g_head; m; m = m->next)
        bytes += m->size;
    return bytes;
}

size_t _num_meta_data_bytes() {
    return _num_allocated_blocks() * sizeof(MallocMetadata);
}

size_t _size_meta_data() {
    return sizeof(MallocMetadata);
}
