#ifndef PICKME_VM_H
#define PICKME_VM_H

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern uint64_t r[16];
#ifndef VM_STACK_SIZE
#define VM_STACK_SIZE (8 * 1024 * 1024)
#endif
extern unsigned char vm_stack[VM_STACK_SIZE];
#define VM_SP 14
#define VM_BP 15
#define VM_DEFINE_MACHINE()                                                                                            \
    uint64_t r[16];                                                                                                    \
    _Alignas(16) unsigned char vm_stack[VM_STACK_SIZE]

#define VM_TRAP() abort()
#define VM_CHECK_STACK()                                                                                               \
    do {                                                                                                               \
        if (r[VM_SP] < (uint64_t)(uintptr_t)vm_stack || r[VM_SP] > (uint64_t)(uintptr_t)(vm_stack + VM_STACK_SIZE))    \
            VM_TRAP();                                                                                                 \
    } while (0)

#define VM_MOV(T, d, a) (r[d] = (uint64_t)(T)r[a])
#define VM_ADDR(d, object) (r[d] = (uint64_t)(uintptr_t) & (object))
/* memcpy keeps unaligned accesses and accesses to byte-array objects defined. */
#define VM_LD(T, d, a, offset)                                                                                         \
    do {                                                                                                               \
        T vm_value;                                                                                                    \
        memcpy(&vm_value, (const void *)(uintptr_t)(r[a] + (uint64_t)(offset)), sizeof(T));                            \
        r[d] = (uint64_t)vm_value;                                                                                     \
    } while (0)
#define VM_ST(T, a, value, offset)                                                                                     \
    do {                                                                                                               \
        T vm_value = (T)r[value];                                                                                      \
        memcpy((void *)(uintptr_t)(r[a] + (uint64_t)(offset)), &vm_value, sizeof(T));                                  \
    } while (0)

/* Unsigned intermediates implement wrapping arithmetic without signed C overflow. */
#define VM_BINARY(T, d, a, op, b) (r[d] = (uint64_t)(T)((uint64_t)(T)r[a] op(uint64_t)(T) r[b]))
#define VM_ADD(T, d, a, b) VM_BINARY(T, d, a, +, b)
#define VM_SUB(T, d, a, b) VM_BINARY(T, d, a, -, b)
#define VM_MUL(T, d, a, b) VM_BINARY(T, d, a, *, b)
#define VM_AND(T, d, a, b) VM_BINARY(T, d, a, &, b)
#define VM_OR(T, d, a, b) VM_BINARY(T, d, a, |, b)
#define VM_XOR(T, d, a, b) VM_BINARY(T, d, a, ^, b)
#define VM_NEG(T, d, a) (r[d] = (uint64_t)(T)(UINT64_C(0) - (uint64_t)(T)r[a]))
#define VM_NOT(T, d, a) (r[d] = (uint64_t)(T) ~(uint64_t)(T)r[a])
#define VM_DIVREM_S(T, d, a, op, b)                                                                                    \
    do {                                                                                                               \
        int64_t vm_a = (int64_t)(T)r[a], vm_b = (int64_t)(T)r[b];                                                      \
        if (!vm_b || (vm_a == INT64_MIN && vm_b == -1)) VM_TRAP();                                                     \
        r[d] = (uint64_t)(T)(vm_a op vm_b);                                                                            \
    } while (0)
#define VM_DIVREM_U(T, d, a, op, b)                                                                                    \
    do {                                                                                                               \
        uint64_t vm_a = (uint64_t)(T)r[a], vm_b = (uint64_t)(T)r[b];                                                   \
        if (!vm_b) VM_TRAP();                                                                                          \
        r[d] = (uint64_t)(T)(vm_a op vm_b);                                                                            \
    } while (0)
#define VM_DIV_S(T, d, a, b) VM_DIVREM_S(T, d, a, /, b)
#define VM_DIV_U(T, d, a, b) VM_DIVREM_U(T, d, a, /, b)
#define VM_REM_S(T, d, a, b) VM_DIVREM_S(T, d, a, %, b)
#define VM_REM_U(T, d, a, b) VM_DIVREM_U(T, d, a, %, b)
#define VM_SHL(T, d, a, b)                                                                                             \
    do {                                                                                                               \
        if (r[b] >= sizeof(T) * 8) VM_TRAP();                                                                          \
        r[d] = (uint64_t)(T)((uint64_t)(T)r[a] << r[b]);                                                               \
    } while (0)
#define VM_SHR(T, d, a, b)                                                                                             \
    do {                                                                                                               \
        if (r[b] >= sizeof(T) * 8) VM_TRAP();                                                                          \
        r[d] = (uint64_t)(T)((T)r[a] >> r[b]);                                                                         \
    } while (0)

#define VM_COMPARE(T, d, a, op, b) (r[d] = ((T)r[a] op(T) r[b]))
#define VM_EQ(T, d, a, b) VM_COMPARE(T, d, a, ==, b)
#define VM_NE(T, d, a, b) VM_COMPARE(T, d, a, !=, b)
#define VM_LT(T, d, a, b) VM_COMPARE(T, d, a, <, b)
#define VM_LE(T, d, a, b) VM_COMPARE(T, d, a, <=, b)
#define VM_GT(T, d, a, b) VM_COMPARE(T, d, a, >, b)
#define VM_GE(T, d, a, b) VM_COMPARE(T, d, a, >=, b)
#define VM_LABEL(label) label:
#define VM_JUMP(label)                                                                                                 \
    do {                                                                                                               \
        goto label;                                                                                                    \
    } while (0)
#define VM_BRANCH(a, yes, no)                                                                                          \
    do {                                                                                                               \
        if (r[a])                                                                                                      \
            goto yes;                                                                                                  \
        else                                                                                                           \
            goto no;                                                                                                   \
    } while (0)

/* C supplies the actual return address. Its 8-byte VM slot preserves the LIR ABI. */
#define VM_CALL(function)                                                                                              \
    do {                                                                                                               \
        r[VM_SP] -= 8;                                                                                                 \
        VM_CHECK_STACK();                                                                                              \
        (function)();                                                                                                  \
    } while (0)
#define VM_ICALL(a)                                                                                                    \
    do {                                                                                                               \
        r[VM_SP] -= 8;                                                                                                 \
        VM_CHECK_STACK();                                                                                              \
        ((void (*)(void))(uintptr_t)r[a])();                                                                           \
    } while (0)
#define VM_RET()                                                                                                       \
    do {                                                                                                               \
        r[VM_SP] += 8;                                                                                                 \
        return;                                                                                                        \
    } while (0)
#define VM_HALT() return

#define VM_OPEN() (r[0] = (uint64_t)(int64_t)open((const char *)(uintptr_t)r[0], (int)r[1], (mode_t)r[2]))
#define VM_READ() (r[0] = (uint64_t)(int64_t)read((int)r[0], (void *)(uintptr_t)r[1], (size_t)r[2]))
#define VM_WRITE() (r[0] = (uint64_t)(int64_t)write((int)r[0], (const void *)(uintptr_t)r[1], (size_t)r[2]))
#define VM_CLOSE() (r[0] = (uint64_t)(int64_t)close((int)r[0]))
#define VM_MALLOC() (r[0] = (uint64_t)(uintptr_t)malloc((size_t)r[0]))
#define VM_FREE()                                                                                                      \
    do {                                                                                                               \
        free((void *)(uintptr_t)r[0]);                                                                                 \
        r[0] = 0;                                                                                                      \
    } while (0)

/* The only function using the host argument ABI is this standard entry point. */
#define VM_MAIN(entry)                                                                                                 \
    int main(int argc, char **argv) {                                                                                  \
        r[0] = (uint64_t)argc;                                                                                         \
        r[1] = (uint64_t)(uintptr_t)argv;                                                                              \
        r[VM_SP] = (uint64_t)(uintptr_t)(vm_stack + VM_STACK_SIZE);                                                    \
        r[VM_BP] = 0;                                                                                                  \
        (entry)();                                                                                                     \
        return (int)(int32_t)r[0];                                                                                     \
    }

#endif
