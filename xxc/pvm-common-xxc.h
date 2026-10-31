/*
 * PVM (++C)
 *
 * Copyright (C) 2026 Daniel McGuire
 * Licensed under the Apache 2.0 License.
 */
#ifndef PVM_COMMON_C_H
#define PVM_COMMON_C_H

#include <def.h>

#define STACKMAX       32
#define CALLMAX        16
#define NRAWREGISTERS  8    /* absolute variables */
#define NREGISTERS     64   /* named variables */
#define NLABELS        64

#define BYTECODE_MAGIC          "PVM!"
#define BYTECODE_MAGIC_LEN      4
#define BYTECODE_VERSION        1   /* header: magic, version */
#define BYTECODE_VERSION_FLAGS  2   /* header: magic, version, flags */
#define FLAG_ARGS               1   /* accepts cli arguments */

typedef enum {
    OP_END = 0, OP_HALT, OP_NOP, OP_PUSH, OP_PUSHS, OP_POP, OP_DUP, OP_SWAP, OP_OVER,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_NEG, OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_NOT, OP_AND, OP_OR, OP_LOAD, OP_STORE, OP_LABEL, OP_JMP, OP_JZ, OP_JNZ, OP_CALL, OP_RET,
    OP_MKHEAP, OP_RMHEAP, OP_LOADW, OP_STOREW, OP_LOADB, OP_STOREB,
    OP_PRINTI, OP_PRINTC, OP_PRINTS, OP_PRINTLN, OP_NEWLINE,
    OP_READI, OP_READC, OP_READLN,
    OP_OPEN, OP_CLOSE, OP_READ, OP_WRITE,
    OP_UNLINK,
    OP_EXPORT, OP_CALLX, OP_LOADMOD,
    OP_ARGC, OP_ARGV,
    OP_ULT, OP_ULE, OP_UGT, OP_UGE,
    OP_XOR, OP_BNOT, OP_SHL, OP_SHR, OP_SAR,
    OP_COUNT
} OpCode;

typedef enum { ARG_NONE, ARG_NUM, ARG_STR, ARG_REGISTER, ARG_LABEL } ArgType;

typedef struct {
    OpCode   op;
    intptr_t arg;
} Instr;

typedef struct {
    OpCode      op;
    const char *name;
    ArgType     arg_type;
    int         pop_count;
    int         push_count;
} OpInfo;

#define OPTAB_SIZE 65
_Static_assert(OP_COUNT == OPTAB_SIZE, "opcode enum and optab size disagree");

static const OpInfo optab[OPTAB_SIZE] = {
    { OP_END,     "end",     ARG_NONE,     0, 0 },
    { OP_HALT,    "halt",    ARG_NUM,      0, 0 },
    { OP_NOP,     "nop",     ARG_NONE,     0, 0 },
    { OP_PUSH,    "push",    ARG_NUM,      0, 1 },
    { OP_PUSHS,   "pushs",   ARG_STR,      0, 1 },
    { OP_POP,     "pop",     ARG_NONE,     1, 0 },
    { OP_DUP,     "dup",     ARG_NONE,     1, 2 },
    { OP_SWAP,    "swap",    ARG_NONE,     2, 2 },
    { OP_OVER,    "over",    ARG_NONE,     2, 3 },
    { OP_ADD,     "add",     ARG_NONE,     2, 1 },
    { OP_SUB,     "sub",     ARG_NONE,     2, 1 },
    { OP_MUL,     "mul",     ARG_NONE,     2, 1 },
    { OP_DIV,     "div",     ARG_NONE,     2, 1 },
    { OP_MOD,     "mod",     ARG_NONE,     2, 1 },
    { OP_NEG,     "neg",     ARG_NONE,     1, 1 },
    { OP_EQ,      "eq",      ARG_NONE,     2, 1 },
    { OP_NE,      "ne",      ARG_NONE,     2, 1 },
    { OP_LT,      "lt",      ARG_NONE,     2, 1 },
    { OP_GT,      "gt",      ARG_NONE,     2, 1 },
    { OP_LE,      "le",      ARG_NONE,     2, 1 },
    { OP_GE,      "ge",      ARG_NONE,     2, 1 },
    { OP_NOT,     "not",     ARG_NONE,     1, 1 },
    { OP_AND,     "and",     ARG_NONE,     2, 1 },
    { OP_OR,      "or",      ARG_NONE,     2, 1 },
    { OP_LOAD,    "load",    ARG_REGISTER, 0, 1 },
    { OP_STORE,   "store",   ARG_REGISTER, 1, 0 },
    { OP_LABEL,   "label",   ARG_LABEL,    0, 0 },
    { OP_JMP,     "jmp",     ARG_LABEL,    0, 0 },
    { OP_JZ,      "jz",      ARG_LABEL,    1, 0 },
    { OP_JNZ,     "jnz",     ARG_LABEL,    1, 0 },
    { OP_CALL,    "call",    ARG_LABEL,    0, 0 },
    { OP_RET,     "ret",     ARG_NONE,     0, 0 },
    { OP_MKHEAP,  "mkheap",  ARG_NONE,     1, 1 },
    { OP_RMHEAP,  "rmheap",  ARG_NONE,     1, 0 },
    { OP_LOADW,   "loadw",   ARG_NONE,     1, 1 },
    { OP_STOREW,  "storew",  ARG_NONE,     2, 0 },
    { OP_LOADB,   "loadb",   ARG_NONE,     1, 1 },
    { OP_STOREB,  "storeb",  ARG_NONE,     2, 0 },
    { OP_PRINTI,  "printi",  ARG_NONE,     1, 0 },
    { OP_PRINTC,  "printc",  ARG_NONE,     1, 0 },
    { OP_PRINTS,  "prints",  ARG_NONE,     1, 0 },
    { OP_PRINTLN, "println", ARG_NONE,     1, 0 },
    { OP_NEWLINE, "newline", ARG_NONE,     0, 0 },
    { OP_READI,   "readi",   ARG_NONE,     0, 2 },
    { OP_READC,   "readc",   ARG_NONE,     0, 1 },
    { OP_READLN,  "readln",  ARG_NONE,     2, 1 },
    { OP_OPEN,    "open",    ARG_NONE,     2, 1 },
    { OP_CLOSE,   "close",   ARG_NONE,     1, 1 },
    { OP_READ,    "read",    ARG_NONE,     3, 1 },
    { OP_WRITE,   "write",   ARG_NONE,     3, 1 },
    { OP_UNLINK,  "delete",  ARG_NONE,     1, 1 },
    { OP_EXPORT,  "export",  ARG_STR,      0, 0 },
    { OP_CALLX,   "callx",   ARG_STR,      0, 0 },
    { OP_LOADMOD, "loadmod", ARG_STR,      0, 1 },
    { OP_ARGC,    "argc",    ARG_NONE,     0, 1 },
    { OP_ARGV,    "argv",    ARG_NONE,     1, 1 },
    { OP_ULT,     "ult",     ARG_NONE,     2, 1 },
    { OP_ULE,     "ule",     ARG_NONE,     2, 1 },
    { OP_UGT,     "ugt",     ARG_NONE,     2, 1 },
    { OP_UGE,     "uge",     ARG_NONE,     2, 1 },
    { OP_XOR,     "xor",     ARG_NONE,     2, 1 },
    { OP_BNOT,    "bnot",    ARG_NONE,     1, 1 },
    { OP_SHL,     "shl",     ARG_NONE,     2, 1 },
    { OP_SHR,     "shr",     ARG_NONE,     2, 1 },
    { OP_SAR,     "sar",     ARG_NONE,     2, 1 }
};

static inline const OpInfo *vm_info(int op)
{
    if (op >= 0 && op < OPTAB_SIZE && (int)optab[op].op == op)
        return &optab[op];
    return nullptr;
}

#endif /* PVM_COMMON_C_H */