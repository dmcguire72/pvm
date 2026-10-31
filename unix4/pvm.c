#define NULL 0
#include "unix4/pvm-common.h"

struct VM {
	struct Instr *vm_prog;
	int vm_pc;
	int vm_at;
	int vm_running;
	int vm_status;
	int vm_trace;
	int vm_heap;
	int vm_sp;
	int vm_csp;
	int vm_stack[STACKMAX];
	int vm_calls[CALLMAX];
	int vm_vars[NREGISTERS];
	int vm_labels[NLABELS];
	int vm_ltop;
	int vm_vtop;
	int vm_nsym;
	int vm_nmod;
	char *vm_symn[NSYMS];
	int vm_symi[NSYMS];
	char *vm_mods[NMODS];
};

extern int fout;
extern flush();

int ofd;
int efd;
int ttyout;

vm_flush()
{
	int *fp;
	int start;

	fp = &fout;
	start = fp + 3;
	if (fp[2] != 0 && fp[2] != start)
		flush();
}

vm_exit(code)
int code;
{
	fout = ofd;
	vm_flush();
	exit(code);
}

struct VM vm;

int inbuf[259];
int in_have;
int in_chr;

char strpool[STRMAX];
char *strptr;
struct Instr program[NPROG];
int prog_size;
char *loaderr;
int loadnum;
int loadflags;
int progflags;
char *pargv[NARGS];
int pargc;

struct OpInfo *vm_info();

struct OpInfo *
vm_info(op)
int op;
{
	register struct OpInfo *oi;

	for (oi = optab; oi->oi_name != NULL; oi++)
		if (oi->oi_op == op)
			return(oi);
	return(NULL);
}

vm_ldfil(filename)
char *filename;
{
	int ibuf[259];
	int op, i, c, val, oprog;
	char *ostr;
	struct OpInfo *oi;

	oprog = prog_size;
	ostr = strptr;
	loadnum = -1;

	if (fopen(filename, ibuf) < 0)
		return(-1);

	if (getc(ibuf) != 'P' ||
	    getc(ibuf) != 'V' ||
	    getc(ibuf) != 'M' ||
	    getc(ibuf) != '!') {
		loaderr = "Invalid bytecode file: bad magic";
		goto bad;
	}

	c = getc(ibuf);
	if (c != BYTECODE_VERSION && c != BYTECODE_VERSION_FLAGS) {
		loaderr = "Unsupported or invalid bytecode version";
		goto bad;
	}
	loadflags = 0;
	if (c == BYTECODE_VERSION_FLAGS) {
		loadflags = getc(ibuf);
		if (loadflags < 0) {
			loaderr = "Truncated header";
			goto bad;
		}
	}

	while ((op = getc(ibuf)) >= 0) {
		if (prog_size >= NPROG) {
			loaderr = "Program too large";
			goto bad;
		}

		program[prog_size].in_op = op;
		oi = vm_info(op);

		if (oi == NULL) {
			loaderr = "Unknown opcode";
			loadnum = op;
			goto bad;
		}

		if (oi->oi_arg == A_STR) {
			program[prog_size].in_arg = strptr;

			for (;;) {
				c = getc(ibuf);
				if (c < 0) {
					loaderr = "Unterminated string";
					goto bad;
				}
				if (strptr >= strpool + STRMAX) {
					loaderr = "String pool full";
					goto bad;
				}
				*strptr++ = c;

				if (c == '\0')
					break;
			}
		} else if (oi->oi_arg != A_NONE) {
			val = 0;

			for (i = 0; i < 8; i++) {
				c = getc(ibuf);

				if (c < 0) {
					loaderr = "Truncated argument";
					goto bad;
				}

				if (i == 0)
					val = c & 0377;

				if (i == 1)
					val = val | ((c & 0377) << 8);
			}

			program[prog_size].in_arg = val;
		} else {
			program[prog_size].in_arg = 0;
		}

		prog_size++;
	}

	close(ibuf[0]);
	return(0);

bad:
	close(ibuf[0]);
	prog_size = oprog;
	strptr = ostr;
	return(-2);
}

load_program(filename)
char *filename;
{
	int r;

	strptr = strpool;
	prog_size = 0;
	r = vm_ldfil(filename);
	progflags = loadflags;
	if (r == -1) {
		printf("Cannot open %s\n", filename);
		vm_exit(1);
	}
	if (r < 0) {
		printf("%s", loaderr);
		if (loadnum >= 0)
			printf(" %d", loadnum);
		printf("\n");
		vm_exit(1);
	}
	return(0);
}

vm_error(v, msg)
struct VM *v;
char *msg;
{
	vm_flush();
	fout = efd;
	printf("vm: %s (instruction %d)\n", msg, v->vm_at);
	vm_flush();
	fout = ofd;
	v->vm_running = 0;
	v->vm_status = 1;
	return(0);
}

vm_push(v, x)
struct VM *v;
int x;
{
	v->vm_stack[v->vm_sp++] = x;
}

vm_pop(v)
struct VM *v;
{
	return(v->vm_stack[--v->vm_sp]);
}

vm_addr(v, a, size)
struct VM *v;
int a, size;
{
	if (a == 0)
		return(vm_error(v, "null pointer"));
	if (size == 2 && (a & 1))
		return(vm_error(v, "unaligned word access"));
	return(1);
}

vm_show(v, i)
struct VM *v;
int i;
{
	register struct Instr *ip;
	register struct OpInfo *oi;

	ip = v->vm_prog + i;
	oi = vm_info(ip->in_op);
	if (oi == NULL) {
		printf("%3d  ???      %d", i, ip->in_op);
		return;
	}
	if (oi->oi_arg == A_NONE) {
		printf("%3d  %s", i, oi->oi_name);
		return;
	}
	printf("%3d  %-8s", i, oi->oi_name);
	switch (oi->oi_arg) {
	case A_NUM:
		printf(" %d", ip->in_arg);
		break;
	case A_STR:
		printf(" \"%s\"", ip->in_arg);
		break;
	case A_REGISTER:
		printf(" v%d", ip->in_arg);
		break;
	case A_LABEL:
		printf(" L%d", ip->in_arg);
		break;
	}
}

vm_stk(v)
struct VM *v;
{
	register int i;

	printf("  [");
	for (i = 0; i < v->vm_sp; i++)
		printf(" %d", v->vm_stack[i]);
	printf(" ]");
}

vm_list(v)
struct VM *v;
{
	register int i;
	int n;

	n = prog_size;
	if (n > 0 && v->vm_prog[n - 1].in_op == OP_END)
		n--;
	for (i = 0; i < n; i++) {
		vm_show(v, i);
		printf("\n");
	}
}

vm_streq(a, b)
char *a, *b;
{
	while (*a == *b) {
		if (*a == 0)
			return(1);
		a++;
		b++;
	}
	return(0);
}

vm_symf(v, name)
struct VM *v;
char *name;
{
	register int i;

	for (i = 0; i < v->vm_nsym; i++)
		if (vm_streq(v->vm_symn[i], name))
			return(i);
	return(-1);
}

vm_fail(v, msg, name)
struct VM *v;
char *msg, *name;
{
	vm_flush();
	fout = efd;
	printf("vm: %s '%s' (instruction %d)\n", msg, name, v->vm_at);
	vm_flush();
	fout = ofd;
	v->vm_running = 0;
	v->vm_status = 1;
	return(0);
}

vm_bind(v, begin, base, vbase)
struct VM *v;
int begin, base, vbase;
{
	register struct Instr *ip;
	register struct OpInfo *oi;
	int i, top, vtop, named;

	top = 0;
	vtop = NRAWREGISTERS;
	for (i = begin; i < prog_size; i++) {
		v->vm_at = i;
		ip = v->vm_prog + i;
		oi = vm_info(ip->in_op);
		if (oi == NULL)
			return(vm_error(v, "bad opcode"));
		if (oi->oi_arg == A_REGISTER) {
			if (ip->in_arg < 0 || ip->in_arg >= NREGISTERS)
				return(vm_error(v, "register out of range"));
			if (ip->in_arg >= vtop)
				vtop = ip->in_arg + 1;
		}
		if (oi->oi_arg == A_LABEL) {
			if (ip->in_arg < 0 || ip->in_arg >= NLABELS)
				return(vm_error(v, "label out of range"));
			if (ip->in_arg >= top)
				top = ip->in_arg + 1;
		}
	}
	if (base + top > NLABELS)
		return(vm_error(v, "too many labels"));

	named = vtop - NRAWREGISTERS;
	if (NRAWREGISTERS + vbase + named > NREGISTERS)
		return(vm_error(v, "too many registers"));

	for (i = begin; i < prog_size; i++) {
		ip = v->vm_prog + i;
		oi = vm_info(ip->in_op);
		if (oi->oi_arg == A_LABEL)
			ip->in_arg = ip->in_arg + base;
		if (oi->oi_arg == A_REGISTER && ip->in_arg >= NRAWREGISTERS)
			ip->in_arg = ip->in_arg + vbase;
	}

	for (i = begin; i < prog_size; i++) {
		v->vm_at = i;
		ip = v->vm_prog + i;
		if (ip->in_op == OP_LABEL) {
			if (v->vm_labels[ip->in_arg] >= 0)
				return(vm_error(v, "duplicate label"));
			v->vm_labels[ip->in_arg] = i;
		}
		if (ip->in_op == OP_EXPORT) {
			if (vm_symf(v, ip->in_arg) >= 0)
				return(vm_fail(v, "duplicate symbol", ip->in_arg));
			if (v->vm_nsym >= NSYMS)
				return(vm_error(v, "too many symbols"));
			v->vm_symn[v->vm_nsym] = ip->in_arg;
			v->vm_symi[v->vm_nsym] = i;
			v->vm_nsym++;
		}
	}

	for (i = begin; i < prog_size; i++) {
		v->vm_at = i;
		ip = v->vm_prog + i;
		oi = vm_info(ip->in_op);
		if (oi->oi_arg == A_LABEL && v->vm_labels[ip->in_arg] < 0)
			return(vm_error(v, "undefined label"));
	}
	if (base + top > v->vm_ltop)
		v->vm_ltop = base + top;
	if (vbase + named > v->vm_vtop)
		v->vm_vtop = vbase + named;
	return(1);
}

vm_dload(v, name)
struct VM *v;
char *name;
{
	int i, r, oprog;

	for (i = 0; i < v->vm_nmod; i++)
		if (vm_streq(v->vm_mods[i], name))
			return(0);
	if (v->vm_nmod >= NMODS) {
		vm_error(v, "too many modules");
		return(-1);
	}
	oprog = prog_size;
	r = vm_ldfil(name);
	if (r == -1)
		return(-1);
	if (r < 0) {
		vm_fail(v, loaderr, name);
		return(-1);
	}
	if (!vm_bind(v, oprog, v->vm_ltop, v->vm_vtop))
		return(-1);
	v->vm_mods[v->vm_nmod++] = name;
	return(0);
}

vm_alu(v, op, a, b)
struct VM *v;
int op, a, b;
{
	switch (op) {
	case OP_ADD:
		return(a + b);
	case OP_SUB:
		return(a - b);
	case OP_MUL:
		return(a * b);
	case OP_DIV:
	case OP_MOD:
		if (b == 0) {
			vm_error(v, "division by zero");
			return(0);
		}
		if (op == OP_DIV)
			return(a / b);
		return(a % b);
	case OP_EQ:
		return(a == b);
	case OP_NE:
		return(a != b);
	case OP_LT:
		return(a < b);
	case OP_GT:
		return(a > b);
	case OP_LE:
		return(a <= b);
	case OP_GE:
		return(a >= b);
	case OP_ULT:
		return((a ^ 0100000) < (b ^ 0100000));
	case OP_ULE:
		return((a ^ 0100000) <= (b ^ 0100000));
	case OP_UGT:
		return((a ^ 0100000) > (b ^ 0100000));
	case OP_UGE:
		return((a ^ 0100000) >= (b ^ 0100000));
	case OP_AND:
		return(a & b);
	case OP_OR:
		return(a | b);
	case OP_XOR:
		return(a ^ b);
	case OP_SHL:
		if (b < 0 || b >= 16)
			return(0);
		return(a << b);
	case OP_SAR:
		if (b < 0 || b >= 16) {
			if (a < 0)
				return(-1);
			return(0);
		}
		return(a >> b);
	case OP_SHR:
		if (b < 0 || b >= 16)
			return(0);
		if (b == 0)
			return(a);
		return((a >> b) & ~(0100000 >> (b - 1)));
	}
	return(0);
}

vm_getc()
{
	if (in_have) {
		in_have = 0;
		return(in_chr);
	}

	if (inbuf[1] <= 0)
		vm_flush();
	return(getc(inbuf));
}

vm_ungetc(c)
int c;
{
	in_have = 1;
	in_chr = c;
}

vm_rdint(v)
struct VM *v;
{
	register int c, n;
	int neg;

	c = vm_getc();
	while (c == ' ' || c == '\t' || c == '\n' || c == '\r')
		c = vm_getc();
	neg = 0;
	if (c == '-' || c == '+') {
		if (c == '-')
			neg = 1;
		c = vm_getc();
	}
	if (c < '0' || c > '9') {
		if (c >= 0)
			vm_ungetc(c);
		vm_push(v, 0);
		vm_push(v, 0);
		return;
	}
	n = 0;
	while (c >= '0' && c <= '9') {
		n = n * 10 + (c - '0');
		c = vm_getc();
	}
	if (c >= 0)
		vm_ungetc(c);
	if (neg)
		n = -n;
	vm_push(v, n);
	vm_push(v, 1);
}

vm_rdline(p, size)
char *p;
int size;
{
	register int c, n;
	int nl, any;

	if (size == 1) {
		p[0] = 0;
		return(0);
	}
	n = 0;
	nl = 0;
	any = 0;
	while (n < size - 1) {
		c = vm_getc();
		if (c < 0)
			break;
		any = 1;
		if (c == '\n') {
			nl = 1;
			break;
		}
		p[n++] = c;
	}
	if (nl && n > 0 && p[n - 1] == '\r')
		n--;
	p[n] = 0;
	if (!any)
		return(-1);
	return(n);
}

vm_fdop(v, path, mode)
struct VM *v;
char *path;
int mode;
{
	if (mode == 0 || mode == 2)
		return(open(path, mode));
	if (mode == 1)
		return(creat(path, 0666));
	vm_error(v, "bad open mode");
	return(-1);
}

vm_fdwr(fd, p, n)
int fd;
char *p;
int n;
{
	register int k;
	int done;

	done = 0;
	while (done < n) {
		k = write(fd, p + done, n - done);
		if (k <= 0)
			break;
		done = done + k;
	}
	if (done == 0 && n > 0)
		return(-1);
	return(done);
}

vm_fdel(path)
char *path;
{
	int sb[18];

	if (stat(path, sb) < 0)
		return(-1);
	if ((sb[2] & 060000) == 040000)
		return(-1);
	return(unlink(path));
}

vm_step(v)
struct VM *v;
{
	register struct Instr *ip;
	register struct OpInfo *oi;
	int a, b, c;
	int *wp;
	char *bp;

	v->vm_at = v->vm_pc;
	if (v->vm_pc < 0 || v->vm_pc >= prog_size) {
		vm_error(v, "program counter out of range");
		return;
	}
	ip = v->vm_prog + v->vm_pc;
	oi = vm_info(ip->in_op);
	if (oi == NULL) {
		vm_error(v, "bad opcode");
		return;
	}
	if (v->vm_trace) {
		vm_flush();
		fout = efd;
		vm_show(v, v->vm_pc);
		vm_stk(v);
		printf("\n");
		vm_flush();
		fout = ofd;
	}
	if (v->vm_sp < oi->oi_pop) {
		vm_error(v, "stack underflow");
		return;
	}
	if (v->vm_sp - oi->oi_pop + oi->oi_push > STACKMAX) {
		vm_error(v, "stack overflow");
		return;
	}
	v->vm_pc++;

	switch (ip->in_op) {
	case OP_END:
		v->vm_running = 0;
		break;
	case OP_HALT:
		v->vm_running = 0;
		v->vm_status = ip->in_arg;
		break;
	case OP_NOP:
	case OP_LABEL:
		break;

	case OP_PUSH:
	case OP_PUSHS:
		vm_push(v, ip->in_arg);
		break;
	case OP_POP:
		vm_pop(v);
		break;
	case OP_DUP:
		a = vm_pop(v);
		vm_push(v, a);
		vm_push(v, a);
		break;
	case OP_SWAP:
		b = vm_pop(v);
		a = vm_pop(v);
		vm_push(v, b);
		vm_push(v, a);
		break;
	case OP_OVER:
		b = vm_pop(v);
		a = vm_pop(v);
		vm_push(v, a);
		vm_push(v, b);
		vm_push(v, a);
		break;

	case OP_ADD:
	case OP_SUB:
	case OP_MUL:
	case OP_DIV:
	case OP_MOD:
	case OP_EQ:
	case OP_NE:
	case OP_LT:
	case OP_GT:
	case OP_LE:
	case OP_GE:
	case OP_ULT:
	case OP_ULE:
	case OP_UGT:
	case OP_UGE:
	case OP_AND:
	case OP_OR:
	case OP_XOR:
	case OP_SHL:
	case OP_SHR:
	case OP_SAR:
		b = vm_pop(v);
		a = vm_pop(v);
		a = vm_alu(v, ip->in_op, a, b);
		if (v->vm_running)
			vm_push(v, a);
		break;
	case OP_NEG:
		vm_push(v, -vm_pop(v));
		break;
	case OP_NOT:
		vm_push(v, !vm_pop(v));
		break;
	case OP_BNOT:
		vm_push(v, ~vm_pop(v));
		break;

	case OP_LOAD:
		vm_push(v, v->vm_vars[ip->in_arg]);
		break;
	case OP_STORE:
		v->vm_vars[ip->in_arg] = vm_pop(v);
		break;

	case OP_JMP:
		v->vm_pc = v->vm_labels[ip->in_arg];
		break;
	case OP_JZ:
		if (vm_pop(v) == 0)
			v->vm_pc = v->vm_labels[ip->in_arg];
		break;
	case OP_JNZ:
		if (vm_pop(v) != 0)
			v->vm_pc = v->vm_labels[ip->in_arg];
		break;
	case OP_CALL:
		if (v->vm_csp >= CALLMAX) {
			vm_error(v, "call stack overflow");
			break;
		}
		v->vm_calls[v->vm_csp++] = v->vm_pc;
		v->vm_pc = v->vm_labels[ip->in_arg];
		break;
	case OP_CALLX:
		a = vm_symf(v, ip->in_arg);
		if (a < 0) {
			vm_fail(v, "undefined symbol", ip->in_arg);
			break;
		}
		if (v->vm_csp >= CALLMAX) {
			vm_error(v, "call stack overflow");
			break;
		}
		v->vm_calls[v->vm_csp++] = v->vm_pc;
		v->vm_pc = v->vm_symi[a];
		break;
	case OP_EXPORT:
		break;
	case OP_LOADMOD:
		a = vm_dload(v, ip->in_arg);
		if (v->vm_running)
			vm_push(v, a);
		break;
	case OP_ARGC:
		vm_push(v, pargc);
		break;
	case OP_ARGV:
		a = vm_pop(v);
		if (a < 0 || a >= pargc)
			vm_push(v, 0);
		else
			vm_push(v, pargv[a]);
		break;
	case OP_RET:
		if (v->vm_csp == 0) {
			vm_error(v, "return without call");
			break;
		}
		v->vm_pc = v->vm_calls[--v->vm_csp];
		break;

	case OP_MKHEAP:
		a = vm_pop(v);
		if (a < 0) {
			vm_error(v, "bad heap size");
			break;
		}
		b = malloc(a);
		if (b != 0)
			v->vm_heap++;
		vm_push(v, b);
		break;
	case OP_RMHEAP:
		a = vm_pop(v);
		if (a != 0) {
			free(a);
			v->vm_heap--;
		}
		break;
	case OP_LOADW:
		a = vm_pop(v);
		if (vm_addr(v, a, 2)) {
			wp = a;
			vm_push(v, *wp);
		}
		break;
	case OP_STOREW:
		b = vm_pop(v);
		a = vm_pop(v);
		if (vm_addr(v, a, 2)) {
			wp = a;
			*wp = b;
		}
		break;
	case OP_LOADB:
		a = vm_pop(v);
		if (vm_addr(v, a, 1)) {
			bp = a;
			vm_push(v, *bp & 0377);
		}
		break;
	case OP_STOREB:
		b = vm_pop(v);
		a = vm_pop(v);
		if (vm_addr(v, a, 1)) {
			bp = a;
			*bp = b;
		}
		break;

	case OP_PRINTI:
		printf("%d", vm_pop(v));
		break;
	case OP_PRINTC:
		putchar(vm_pop(v));
		break;
	case OP_PRINTS:
		a = vm_pop(v);
		if (vm_addr(v, a, 1))
			printf("%s", a);
		break;
	case OP_PRINTLN:
		a = vm_pop(v);
		if (vm_addr(v, a, 1))
			printf("%s\n", a);
		if (ttyout)
			vm_flush();
		break;
	case OP_NEWLINE:
		putchar('\n');
		if (ttyout)
			vm_flush();
		break;

	case OP_READI:
		vm_rdint(v);
		break;
	case OP_READC:
		vm_push(v, vm_getc());
		break;
	case OP_READLN:
		b = vm_pop(v);
		a = vm_pop(v);
		if (!vm_addr(v, a, 1))
			break;
		if (b <= 0) {
			vm_error(v, "bad buffer size");
			break;
		}
		vm_push(v, vm_rdline(a, b));
		break;
	case OP_OPEN:
		b = vm_pop(v);
		a = vm_pop(v);
		if (!vm_addr(v, a, 1))
			break;
		vm_push(v, vm_fdop(v, a, b));
		break;
	case OP_CLOSE:
		a = vm_pop(v);
		vm_push(v, close(a));
		break;
	case OP_DELETE:
		a = vm_pop(v);
		if (!vm_addr(v, a, 1))
			break;
		vm_push(v, vm_fdel(a));
		break;
	case OP_READ:
		c = vm_pop(v);
		b = vm_pop(v);
		a = vm_pop(v);
		if (!vm_addr(v, b, 1))
			break;
		if (c < 0) {
			vm_error(v, "bad count");
			break;
		}
		if (a >= 0 && a <= 2)
			vm_flush();
		vm_push(v, read(a, b, c));
		break;
	case OP_WRITE:
		c = vm_pop(v);
		b = vm_pop(v);
		a = vm_pop(v);
		if (!vm_addr(v, b, 1))
			break;
		if (c < 0) {
			vm_error(v, "bad count");
			break;
		}
		if (a >= 0 && a <= 2)
			vm_flush();
		vm_push(v, vm_fdwr(a, b, c));
		break;
	}
}

vm_run(v)
struct VM *v;
{
	register int i;

	for (i = 0; i < NLABELS; i++)
		v->vm_labels[i] = -1;
	v->vm_ltop = 0;
	v->vm_vtop = 0;
	v->vm_nsym = 0;
	v->vm_nmod = 0;
	if (!vm_bind(v, 0, 0, 0))
		return(v->vm_status);
	v->vm_pc = 0;
	v->vm_running = 1;
	while (v->vm_running)
		vm_step(v);
	if (v->vm_heap > 0) {
		vm_flush();
		fout = efd;
		printf("vm: warning: %d heap block(s) not freed\n", v->vm_heap);
		vm_flush();
		fout = ofd;
	}
	return(v->vm_status);
}

main(argc, argv)
int argc;
char *argv[];
{
	register int i;
	int list;
	int ttyb[3];
	char *filename;

	ofd = dup(1);
	if (ofd < 0)
		ofd = 1;
	efd = dup(2);
	if (efd < 0)
		efd = 2;
	fout = ofd;
	if (gtty(1, ttyb) == 0)
		ttyout = 1;

	list = 0;
	filename = NULL;
	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-' && argv[i][1] == 't' && argv[i][2] == 0)
			vm.vm_trace = 1;
		else if (argv[i][0] == '-' && argv[i][1] == 'd' && argv[i][2] == 0)
			list = 1;
		else if (argv[i][0] == '-' && argv[i][1] == '-' && argv[i][2] == 0) {
			i++;
			break;
		} else if (argv[i][0] != '-')
			break;
		else {
			fout = efd;
			printf("usage: vm [-t] [-d] [prog.bin [args...]]\n");
			vm_flush();
			vm_exit(2);
		}
	}

	if (i < argc)
		filename = argv[i++];

	if (filename == NULL)
		filename = "p.out";

	load_program(filename);

	vm.vm_prog = program;
	if (!list) {
		if (progflags & FLAG_ARGS) {
			if (argc - i + 1 > NARGS) {
				fout = efd;
				printf("vm: too many arguments (max %d)\n", NARGS - 1);
				vm_flush();
				vm_exit(2);
			}
			pargv[0] = filename;
			pargc = 1;
			while (i < argc)
				pargv[pargc++] = argv[i++];
		} else if (i < argc) {
			fout = efd;
			printf("vm: %s does not accept arguments (it needs the 'args' directive)\n", filename);
			vm_flush();
			vm_exit(2);
		}
	}
	if (list) {
		vm_list(&vm);
		vm_exit(0);
	}
	vm_exit(vm_run(&vm));
}
