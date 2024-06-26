#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

// Macro to avoid having to cast back to the desired type.
#define ALLOCATE_OBJ(type, objectType)  \
    (type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type) {
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;

    // This object points to the head of the linked-list.
    object->next = vm.objects;
    vm.objects = object;
    return object;
}


static ObjString* allocateString(char* chars, int length,
                                 uint32_t hash) {
    ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length = length;
    string->chars  = chars;
    string->hash   = hash;
    
    return string;
}

/* FNV-1a hash-function */
static uint32_t hashString(const char* key, int length) {
    uint32_t hash = 2166136261u;

    for (int i = 0; i < length; ++i) {
        hash ^= key[i];
        hash *= 16777619;
    }
    return hash;
}

ObjString* takeString(char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString* interned = tableFindString(&vm.strings, chars, length,
                                          hash);

    // Check if we've interned this string already.
    if (interned != NULL) {
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }
    return allocateString(chars, length, hash);
}

ObjString* copyString(const char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString* interned = tableFindString(&vm.strings, chars, length,
                                          hash);
    // Check if we've interned this string in our strings map.
    // If so, return a pointer to that string.
    if (interned != NULL) return interned;

    char* heapChars = ALLOCATE(char, length +1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0'; // Null terminate the string!
    
    return allocateString(heapChars, length, hash);
}

void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;    
        }
}
