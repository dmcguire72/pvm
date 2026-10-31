/*
 * PVM for Research Unix V4 (UNIX Fourth Edition)
 *
 * Copyright (C) 2026 Daniel McGuire
 * Licensed under the Apache 2.0 License.
 */

#define STACKMAX	32
#define CALLMAX		16
#define NRAWREGISTERS	8
#define NREGISTERS		64
#define NLABELS		128
#define NSYMS		64
#define NMODS		8
#define NPROG		2048
#define STRMAX		4096

#define BYTECODE_VERSION 1
#define BYTECODE_VERSION_FLAGS 2
#define FLAG_ARGS	1
#define NARGS		16

#define OP_END		0
#define OP_HALT		1
#define OP_NOP		2
#define OP_PUSH		3
#define OP_PUSHS	4
#define OP_POP		5
#define OP_DUP		6
#define OP_SWAP		7
#define OP_OVER		8
#define OP_ADD		9
#define OP_SUB		10
#define OP_MUL		11
#define OP_DIV		12
#define OP_MOD		13
#define OP_NEG		14
#define OP_EQ		15
#define OP_NE		16
#define OP_LT		17
#define OP_GT		18
#define OP_LE		19
#define OP_GE		20
#define OP_NOT		21
#define OP_AND		22
#define OP_OR		23
#define OP_LOAD		24
#define OP_STORE	25
#define OP_LABEL	26
#define OP_JMP		27
#define OP_JZ		28
#define OP_JNZ		29
#define OP_CALL		30
#define OP_RET		31
#define OP_MKHEAP	32
#define OP_RMHEAP	33
#define OP_LOADW	34
#define OP_STOREW	35
#define OP_LOADB	36
#define OP_STOREB	37
#define OP_PRINTI	38
#define OP_PRINTC	39
#define OP_PRINTS	40
#define OP_PRINTLN	41
#define OP_NEWLINE	42
#define OP_READI	43
#define OP_READC	44
#define OP_READLN	45
#define OP_OPEN		46
#define OP_CLOSE	47
#define OP_READ		48
#define OP_WRITE	49
#define OP_DELETE	50
#define OP_EXPORT	51
#define OP_CALLX	52
#define OP_LOADMOD	53
#define OP_ARGC		54
#define OP_ARGV		55
#define OP_ULT		56
#define OP_ULE		57
#define OP_UGT		58
#define OP_UGE		59
#define OP_XOR		60
#define OP_BNOT		61
#define OP_SHL		62
#define OP_SHR		63
#define OP_SAR		64

#define A_NONE		0
#define A_NUM		1
#define A_STR		2
#define A_REGISTER		3
#define A_LABEL		4



struct Instr {
	int in_op;
	int in_arg;
};

struct OpInfo {
	int oi_op;
	char *oi_name;
	int oi_arg;
	int oi_pop;
	int oi_push;
};

struct OpInfo optab[] {
	OP_END,		"end",		A_NONE,		0, 0,
	OP_HALT,	"halt",		A_NUM,		0, 0,
	OP_NOP,		"nop",		A_NONE,		0, 0,
	OP_PUSH,	"push",		A_NUM,		0, 1,
	OP_PUSHS,	"pushs",	A_STR,		0, 1,
	OP_POP,		"pop",		A_NONE,		1, 0,
	OP_DUP,		"dup",		A_NONE,		1, 2,
	OP_SWAP,	"swap",		A_NONE,		2, 2,
	OP_OVER,	"over",		A_NONE,		2, 3,
	OP_ADD,		"add",		A_NONE,		2, 1,
	OP_SUB,		"sub",		A_NONE,		2, 1,
	OP_MUL,		"mul",		A_NONE,		2, 1,
	OP_DIV,		"div",		A_NONE,		2, 1,
	OP_MOD,		"mod",		A_NONE,		2, 1,
	OP_NEG,		"neg",		A_NONE,		1, 1,
	OP_EQ,		"eq",		A_NONE,		2, 1,
	OP_NE,		"ne",		A_NONE,		2, 1,
	OP_LT,		"lt",		A_NONE,		2, 1,
	OP_GT,		"gt",		A_NONE,		2, 1,
	OP_LE,		"le",		A_NONE,		2, 1,
	OP_GE,		"ge",		A_NONE,		2, 1,
	OP_NOT,		"not",		A_NONE,		1, 1,
	OP_AND,		"and",		A_NONE,		2, 1,
	OP_OR,		"or",		A_NONE,		2, 1,
	OP_LOAD,	"load",		A_REGISTER,		0, 1,
	OP_STORE,	"store",	A_REGISTER,		1, 0,
	OP_LABEL,	"label",	A_LABEL,	0, 0,
	OP_JMP,		"jmp",		A_LABEL,	0, 0,
	OP_JZ,		"jz",		A_LABEL,	1, 0,
	OP_JNZ,		"jnz",		A_LABEL,	1, 0,
	OP_CALL,	"call",		A_LABEL,	0, 0,
	OP_RET,		"ret",		A_NONE,		0, 0,
	OP_MKHEAP,	"mkheap",	A_NONE,		1, 1,
	OP_RMHEAP,	"rmheap",	A_NONE,		1, 0,
	OP_LOADW,	"loadw",	A_NONE,		1, 1,
	OP_STOREW,	"storew",	A_NONE,		2, 0,
	OP_LOADB,	"loadb",	A_NONE,		1, 1,
	OP_STOREB,	"storeb",	A_NONE,		2, 0,
	OP_PRINTI,	"printi",	A_NONE,		1, 0,
	OP_PRINTC,	"printc",	A_NONE,		1, 0,
	OP_PRINTS,	"prints",	A_NONE,		1, 0,
	OP_PRINTLN,	"println",	A_NONE,		1, 0,
	OP_NEWLINE,	"newline",	A_NONE,		0, 0,
	OP_READI,	"readi",	A_NONE,		0, 2,
	OP_READC,	"readc",	A_NONE,		0, 1,
	OP_READLN,	"readln",	A_NONE,		2, 1,
	OP_OPEN,	"open",		A_NONE,		2, 1,
	OP_CLOSE,	"close",	A_NONE,		1, 1,
	OP_READ,	"read",		A_NONE,		3, 1,
	OP_WRITE,	"write",	A_NONE,		3, 1,
	OP_DELETE,	"delete",	A_NONE,		1, 1,
	OP_EXPORT,	"export",	A_STR,		0, 0,
	OP_CALLX,	"callx",	A_STR,		0, 0,
	OP_LOADMOD,	"loadmod",	A_STR,		0, 1,
	OP_ARGC,	"argc",		A_NONE,		0, 1,
	OP_ARGV,	"argv",		A_NONE,		1, 1,
	OP_ULT,		"ult",		A_NONE,		2, 1,
	OP_ULE,		"ule",		A_NONE,		2, 1,
	OP_UGT,		"ugt",		A_NONE,		2, 1,
	OP_UGE,		"uge",		A_NONE,		2, 1,
	OP_XOR,		"xor",		A_NONE,		2, 1,
	OP_BNOT,	"bnot",		A_NONE,		1, 1,
	OP_SHL,		"shl",		A_NONE,		2, 1,
	OP_SHR,		"shr",		A_NONE,		2, 1,
	OP_SAR,		"sar",		A_NONE,		2, 1,
	-1,		0,		0,		0, 0
};