#include <unistd.h>
#include <cstddef>

#define MAX_ALLOC_SIZE (1e8)

void* smalloc(size_t size) {
    if (size == 0 || size > MAX_ALLOC_SIZE)
        return nullptr;

    void* ptr = sbrk((intptr_t)size);
    if (ptr == (void*)(-1))
        return nullptr;

    return ptr;
}
