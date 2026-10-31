/*
 * PVM
 *
 * Copyright (C) 2026 Daniel McGuire
 * Licensed under the Apache 2.0 License.
 */
#pragma once

#include <vector>
#include <array>
#include <string>
#include <cstdint>

constexpr int STACKMAX = 32;
constexpr int CALLMAX  = 16;
constexpr int NRAWREGISTERS = 8;    // absolute variables
constexpr int NREGISTERS    = 64;   // named variables
constexpr int NLABELS  = 64;

constexpr char BYTECODE_MAGIC[] = "PVM!";
constexpr uint8_t BYTECODE_VERSION = 1;        // header: magic, version
constexpr uint8_t BYTECODE_VERSION_FLAGS = 2;  // header: magic, version, flags
constexpr uint8_t FLAG_ARGS = 1;               // accepts cli argumens

enum class OpCode {
    END = 0, HALT, NOP, PUSH, PUSHS, POP, DUP, SWAP, OVER,
    ADD, SUB, MUL, DIV, MOD, NEG, EQ, NE, LT, GT, LE, GE,
    NOT, AND, OR, LOAD, STORE, LABEL, JMP, JZ, JNZ, CALL, RET,
    MKHEAP, RMHEAP, LOADW, STOREW, LOADB, STOREB,
    PRINTI, PRINTC, PRINTS, PRINTLN, NEWLINE,
    READI, READC, READLN,
    OPEN, CLOSE, READ, WRITE,
    UNLINK,
    EXPORT, CALLX, LOADMOD,
    ARGC, ARGV,
    ULT, ULE, UGT, UGE,
    XOR, BNOT, SHL, SHR, SAR
};

enum class ArgType { NONE, NUM, STR, REGISTER, LABEL };

struct Instr {
    OpCode in_op;
    intptr_t in_arg;

    Instr(OpCode op, intptr_t arg = 0) : in_op(op), in_arg(arg) {}
};

struct OpInfo {
    OpCode op;
    const char* name;
    ArgType arg_type;
    int pop_count;
    int push_count;
};

constexpr std::array<OpInfo, 65> optab = {{
    { OpCode::END,     "end",     ArgType::NONE,  0, 0 },
    { OpCode::HALT,    "halt",    ArgType::NUM,   0, 0 },
    { OpCode::NOP,     "nop",     ArgType::NONE,  0, 0 },
    { OpCode::PUSH,    "push",    ArgType::NUM,   0, 1 },
    { OpCode::PUSHS,   "pushs",   ArgType::STR,   0, 1 },
    { OpCode::POP,     "pop",     ArgType::NONE,  1, 0 },
    { OpCode::DUP,     "dup",     ArgType::NONE,  1, 2 },
    { OpCode::SWAP,    "swap",    ArgType::NONE,  2, 2 },
    { OpCode::OVER,    "over",    ArgType::NONE,  2, 3 },
    { OpCode::ADD,     "add",     ArgType::NONE,  2, 1 },
    { OpCode::SUB,     "sub",     ArgType::NONE,  2, 1 },
    { OpCode::MUL,     "mul",     ArgType::NONE,  2, 1 },
    { OpCode::DIV,     "div",     ArgType::NONE,  2, 1 },
    { OpCode::MOD,     "mod",     ArgType::NONE,  2, 1 },
    { OpCode::NEG,     "neg",     ArgType::NONE,  1, 1 },
    { OpCode::EQ,      "eq",      ArgType::NONE,  2, 1 },
    { OpCode::NE,      "ne",      ArgType::NONE,  2, 1 },
    { OpCode::LT,      "lt",      ArgType::NONE,  2, 1 },
    { OpCode::GT,      "gt",      ArgType::NONE,  2, 1 },
    { OpCode::LE,      "le",      ArgType::NONE,  2, 1 },
    { OpCode::GE,      "ge",      ArgType::NONE,  2, 1 },
    { OpCode::NOT,     "not",     ArgType::NONE,  1, 1 },
    { OpCode::AND,     "and",     ArgType::NONE,  2, 1 },
    { OpCode::OR,      "or",      ArgType::NONE,  2, 1 },
    { OpCode::LOAD,    "load",    ArgType::REGISTER,   0, 1 },
    { OpCode::STORE,   "store",   ArgType::REGISTER,   1, 0 },
    { OpCode::LABEL,   "label",   ArgType::LABEL, 0, 0 },
    { OpCode::JMP,     "jmp",     ArgType::LABEL, 0, 0 },
    { OpCode::JZ,      "jz",      ArgType::LABEL, 1, 0 },
    { OpCode::JNZ,     "jnz",     ArgType::LABEL, 1, 0 },
    { OpCode::CALL,    "call",    ArgType::LABEL, 0, 0 },
    { OpCode::RET,     "ret",     ArgType::NONE,  0, 0 },
    { OpCode::MKHEAP,  "mkheap",  ArgType::NONE,  1, 1 },
    { OpCode::RMHEAP,  "rmheap",  ArgType::NONE,  1, 0 },
    { OpCode::LOADW,   "loadw",   ArgType::NONE,  1, 1 },
    { OpCode::STOREW,  "storew",  ArgType::NONE,  2, 0 },
    { OpCode::LOADB,   "loadb",   ArgType::NONE,  1, 1 },
    { OpCode::STOREB,  "storeb",  ArgType::NONE,  2, 0 },
    { OpCode::PRINTI,  "printi",  ArgType::NONE,  1, 0 },
    { OpCode::PRINTC,  "printc",  ArgType::NONE,  1, 0 },
    { OpCode::PRINTS,  "prints",  ArgType::NONE,  1, 0 },
    { OpCode::PRINTLN, "println", ArgType::NONE,  1, 0 },
    { OpCode::NEWLINE, "newline", ArgType::NONE,  0, 0 },
    { OpCode::READI,   "readi",   ArgType::NONE,  0, 2 },
    { OpCode::READC,   "readc",   ArgType::NONE,  0, 1 },
    { OpCode::READLN,  "readln",  ArgType::NONE,  2, 1 },
    { OpCode::OPEN,    "open",    ArgType::NONE,  2, 1 },
    { OpCode::CLOSE,   "close",   ArgType::NONE,  1, 1 },
    { OpCode::READ,    "read",    ArgType::NONE,  3, 1 },
    { OpCode::WRITE,   "write",   ArgType::NONE,  3, 1 },
    { OpCode::UNLINK,  "delete",  ArgType::NONE,  1, 1 },
    { OpCode::EXPORT,  "export",  ArgType::STR,   0, 0 },
    { OpCode::CALLX,   "callx",   ArgType::STR,   0, 0 },
    { OpCode::LOADMOD, "loadmod", ArgType::STR,   0, 1 },
    { OpCode::ARGC,    "argc",    ArgType::NONE,  0, 1 },
    { OpCode::ARGV,    "argv",    ArgType::NONE,  1, 1 },
    { OpCode::ULT,     "ult",     ArgType::NONE,  2, 1 },
    { OpCode::ULE,     "ule",     ArgType::NONE,  2, 1 },
    { OpCode::UGT,     "ugt",     ArgType::NONE,  2, 1 },
    { OpCode::UGE,     "uge",     ArgType::NONE,  2, 1 },
    { OpCode::XOR,     "xor",     ArgType::NONE,  2, 1 },
    { OpCode::BNOT,    "bnot",    ArgType::NONE,  1, 1 },
    { OpCode::SHL,     "shl",     ArgType::NONE,  2, 1 },
    { OpCode::SHR,     "shr",     ArgType::NONE,  2, 1 },
    { OpCode::SAR,     "sar",     ArgType::NONE,  2, 1 }
}};

inline const OpInfo* vm_info(OpCode op) {
    size_t index = static_cast<size_t>(op);
    if (index < optab.size() && optab[index].op == op) {
        return &optab[index];
    }
    return nullptr;
}