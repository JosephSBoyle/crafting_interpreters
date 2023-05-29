#include <stdlib.h>

#include "memory.h"

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* ptr = realloc(pointer, newSize);
    if (!ptr) exit(1); // realloc failed
    return ptr;
}
