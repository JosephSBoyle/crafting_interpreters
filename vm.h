#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "value.h"

#define STACK_MAX 256

typedef struct {
    Chunk*   chunk;
    uint8_t* ip;
    // IP: instruction pointer - points to the current instruction.
    
    Value  stack[STACK_MAX];
    Value* stackTop;
    /* Pointer to the next empty slot at the top of the stack.
    It's quicker to simply dereference the pointer
    than to keep computing it using an offset.*/
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);

void  push(Value value);
Value pop();

#endif