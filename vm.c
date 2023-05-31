#include <stdio.h>

#include "common.h"
#include "debug.h"
#include "vm.h"

// Global singleton VirtualMachine object.
VM vm;

void initVM() {}

void freeVM() {}

static InterpretResult run() {
// dereferences the current instruction pointer
// and advances to the next instruction.
#define READ_BYTE() (*vm.ip++)

// get's the value of a constant corresponding to the
// the current instruction pointer and advances the pointer.
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])

    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        // The instruction pointer is absolute but we need an
        // offset for the second argument here.
        disassembleInstruction(vm.chunk,
                               (int)(vm.ip - vm.chunk->code));
#endif
        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                printValue(constant);
                printf("\n");
                break;
            }
            case OP_RETURN: {
                return INTERPRET_OK;
            }
        }
    }

#undef READ_BYTE
#undef READ_CONSTANT    
}


InterpretResult interpret(Chunk* chunk) {
    vm.chunk = chunk;
    
    // The vm's instruction pointer should start off
    // pointing to the first bytecode operation.
    vm.ip = vm.chunk->code;
    return run();
}