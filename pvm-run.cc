/*
 * PVM
 *
 * Copyright (C) 2026 Daniel McGuire
 * Licensed under the Apache 2.0 License.
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <list>
#include <set>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <iomanip>
#include <stdexcept>
#include <cstdlib>
#include <cctype>
#include <climits>
#include "pvm-common.h"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace sys {
#ifdef _WIN32
    constexpr int RDONLY = _O_RDONLY | _O_BINARY;
    constexpr int WRONLY = _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY;
    constexpr int RDWR   = _O_RDWR | _O_BINARY;

    inline int open(const char* path, int flags) {
        return _open(path, flags, _S_IREAD | _S_IWRITE);
    }
    inline int close(int fd) { return _close(fd); }
    inline int unlink(const char* path) { return _unlink(path); }
    inline intptr_t read(int fd, void* buf, size_t n) {
        if (n > INT_MAX) n = INT_MAX;
        return _read(fd, buf, static_cast<unsigned>(n));
    }
    inline intptr_t write(int fd, const void* buf, size_t n) {
        if (n > INT_MAX) n = INT_MAX;
        return _write(fd, buf, static_cast<unsigned>(n));
    }
#else
    constexpr int RDONLY = O_RDONLY;
    constexpr int WRONLY = O_WRONLY | O_CREAT | O_TRUNC;
    constexpr int RDWR   = O_RDWR;

    inline int open(const char* path, int flags) {
        return ::open(path, flags, 0666);
    }
    inline int close(int fd) { return ::close(fd); }
    inline int unlink(const char* path) { return ::unlink(path); }
    inline intptr_t read(int fd, void* buf, size_t n) {
        return ::read(fd, buf, n);
    }
    inline intptr_t write(int fd, const void* buf, size_t n) {
        return ::write(fd, buf, n);
    }
#endif
}

int64_t read_le64(std::istream& is) {
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        int c = is.get();
        if (c == EOF) throw std::runtime_error("Unexpected EOF while reading little-endian argument");
        uint64_t b = static_cast<uint8_t>(c);
        val |= (b << (i * 8));
    }
    return static_cast<int64_t>(val);
}

enum class LoadStatus { OK, NOT_FOUND, INVALID };

LoadStatus read_module(const std::string& filename, std::vector<Instr>& out,
                       std::list<std::string>& pool, std::string& err,
                       uint8_t* flags = nullptr) {
    std::ifstream in(filename, std::ios::binary);
    if (!in) {
        err = "Failed to open bytecode file: " + filename;
        return LoadStatus::NOT_FOUND;
    }

    std::string magic(sizeof(BYTECODE_MAGIC) - 1, '\0');
    if (!in.read(&magic[0], static_cast<std::streamsize>(magic.size())) ||
        magic != BYTECODE_MAGIC) {
        err = "Invalid bytecode file: bad magic.";
        return LoadStatus::INVALID;
    }

    int version = in.get();
    if (version != BYTECODE_VERSION && version != BYTECODE_VERSION_FLAGS) {
        err = "Unsupported or invalid bytecode version.";
        return LoadStatus::INVALID;
    }
    int file_flags = 0;
    if (version == BYTECODE_VERSION_FLAGS) {
        file_flags = in.get();
        if (file_flags == EOF) {
            err = "Corrupted binary: truncated header.";
            return LoadStatus::INVALID;
        }
    }
    if (flags) *flags = static_cast<uint8_t>(file_flags);

    while (in.peek() != EOF) {
        int c = in.get();
        OpCode op = static_cast<OpCode>(c);
        const OpInfo* oi = vm_info(op);
        if (!oi) {
            err = "Corrupted binary: invalid opcode detected.";
            return LoadStatus::INVALID;
        }

        intptr_t arg = 0;
        if (oi->arg_type == ArgType::STR) {
            std::string s;
            char ch;
            bool terminated = false;
            while (in.get(ch)) {
                if (ch == '\0') { terminated = true; break; }
                s += ch;
            }
            if (!terminated) {
                err = "Corrupted binary: unterminated string.";
                return LoadStatus::INVALID;
            }
            pool.push_back(s);
            arg = reinterpret_cast<intptr_t>(pool.back().c_str());
        } else if (oi->arg_type != ArgType::NONE) {
            try {
                arg = static_cast<intptr_t>(read_le64(in));
            } catch (const std::runtime_error&) {
                err = "Corrupted binary: truncated argument.";
                return LoadStatus::INVALID;
            }
        }

        out.push_back({op, arg});
    }
    return LoadStatus::OK;
}

class VM {
public:
    std::string load_main(const std::string& filename) {
        std::string err;
        if (read_module(filename, prog_, pool_, err, &flags_) != LoadStatus::OK) return err;
        return "";
    }

    bool accepts_args() const { return (flags_ & FLAG_ARGS) != 0; }
    void set_args(std::vector<std::string> args) { args_ = std::move(args); }
    void set_trace(bool on) { trace_ = on; }

    void list() const {
        size_t n = prog_.size();
        if (n > 0 && prog_[n - 1].in_op == OpCode::END) n--;
        for (size_t i = 0; i < n; i++) {
            show(static_cast<int>(i));
            std::cout << '\n';
        }
    }

    int run() {
        labels_.fill(-1);
        label_top_ = 0;
        var_top_ = 0;
        symbols_.clear();
        loaded_.clear();
        link_module(0, 0, 0);
        pc_ = 0;
        running_ = true;
        while (running_) step();
        if (heap_ > 0)
            std::cerr << "vm: warning: " << heap_ << " heap block(s) not freed\n";
        return status_;
    }

private:
    std::vector<Instr> prog_;
    uint8_t flags_ = 0;
    std::vector<std::string> args_;
    std::list<std::string> pool_;
    std::unordered_map<std::string, int> symbols_;
    std::set<std::string> loaded_;
    int label_top_ = 0;
    int var_top_ = 0;
    int pc_ = 0;
    int at_ = 0;
    bool running_ = false;
    int status_ = 0;
    bool trace_ = false;
    int heap_ = 0;
    int sp_ = 0;
    int csp_ = 0;
    std::array<intptr_t, STACKMAX> stack_{};
    std::array<int,     CALLMAX>  calls_{};
    std::array<intptr_t, NREGISTERS>   vars_{};
    std::array<int,     NLABELS>  labels_{};

    [[noreturn]] void error(const std::string& msg) {
        running_ = false;
        status_ = 1;
        throw std::runtime_error(
            "vm: " + msg + " (instruction " + std::to_string(at_) + ")");
    }

    void push(intptr_t x) { stack_[sp_++] = x; }
    intptr_t pop()        { return stack_[--sp_]; }

    void addr(intptr_t a, int size) {
        if (a == 0)               error("null pointer");
        if (size == 2 && (a & 1)) error("unaligned word access");
    }

    bool read_int(intptr_t& out) {
        int c = std::cin.peek();
        while (c != EOF && std::isspace(c)) {
            std::cin.get();
            c = std::cin.peek();
        }
        bool neg = false;
        if (c == '-' || c == '+') {
            neg = (c == '-');
            std::cin.get();
            c = std::cin.peek();
        }
        if (c == EOF || !std::isdigit(c)) return false;

        uint64_t v = 0;
        while (c != EOF && std::isdigit(c)) {
            v = v * 10 + static_cast<uint64_t>(c - '0');
            std::cin.get();
            c = std::cin.peek();
        }
        out = static_cast<intptr_t>(neg ? (0 - v) : v);
        return true;
    }

    intptr_t read_line(uint8_t* buf, intptr_t size) {
        if (size == 1) { buf[0] = 0; return 0; }
        intptr_t n = 0;
        bool got_any = false;
        bool got_nl = false;
        while (n < size - 1) {
            int c = std::cin.get();
            if (c == EOF) break;
            got_any = true;
            if (c == '\n') { got_nl = true; break; }
            buf[n++] = static_cast<uint8_t>(c);
        }
        if (got_nl && n > 0 && buf[n - 1] == '\r') n--;
        buf[n] = 0;
        if (!got_any && n == 0) {
            std::cin.clear();
            return -1;
        }
        return n;
    }

    intptr_t fd_write(int fd, const uint8_t* p, intptr_t n) {
        intptr_t done = 0;
        while (done < n) {
            intptr_t k = sys::write(fd, p + done, static_cast<size_t>(n - done));
            if (k <= 0) break;
            done += k;
        }
        if (done == 0 && n > 0) return -1;
        return done;
    }

    void sync_std(int fd) {
        if (fd >= 0 && fd <= 2) std::cout.flush();
    }

    void show(int i) const {
        const Instr& ip = prog_[i];
        const OpInfo* oi = vm_info(ip.in_op);

        if (oi == nullptr) {
            std::cout << std::setw(3) << i << "  ???      " << static_cast<int>(ip.in_op);
            return;
        }
        if (oi->arg_type == ArgType::NONE) {
            std::cout << std::setw(3) << i << "  " << oi->name;
            return;
        }

        std::cout << std::setw(3) << i << "  "
                  << std::left << std::setw(8) << oi->name << std::right;
        switch (oi->arg_type) {
            case ArgType::NUM:   std::cout << " " << ip.in_arg; break;
            case ArgType::STR:   std::cout << " \"" << reinterpret_cast<const char*>(ip.in_arg) << "\""; break;
            case ArgType::REGISTER:   std::cout << " r" << ip.in_arg; break;
            case ArgType::LABEL: std::cout << " L" << ip.in_arg; break;
            default: break;
        }
    }

    void stk() const {
        std::cout << "  [";
        for (int i = 0; i < sp_; i++) std::cout << " " << stack_[i];
        std::cout << " ]";
    }

    void link_module(size_t begin, int base, int var_base) {
        int top = 0;
        int vtop = NRAWREGISTERS;
        for (size_t i = begin; i < prog_.size(); i++) {
            at_ = static_cast<int>(i);
            const Instr& ip = prog_[i];
            const OpInfo* oi = vm_info(ip.in_op);

            if (oi == nullptr) error("bad opcode");
            if (oi->arg_type == ArgType::REGISTER) {
                if (ip.in_arg < 0 || ip.in_arg >= NREGISTERS) error("register out of range");
                vtop = std::max<int>(vtop, static_cast<int>(ip.in_arg) + 1);
            }
            if (oi->arg_type == ArgType::LABEL) {
                if (ip.in_arg < 0 || ip.in_arg >= NLABELS) error("label out of range");
                top = std::max<int>(top, static_cast<int>(ip.in_arg) + 1);
            }
        }
        if (base + top > NLABELS) error("too many labels");

        const int named = vtop - NRAWREGISTERS;
        if (NRAWREGISTERS + var_base + named > NREGISTERS) error("too many variables");

        for (size_t i = begin; i < prog_.size(); i++) {
            const OpInfo* oi = vm_info(prog_[i].in_op);
            if (oi->arg_type == ArgType::LABEL) prog_[i].in_arg += base;
            if (oi->arg_type == ArgType::REGISTER && prog_[i].in_arg >= NRAWREGISTERS)
                prog_[i].in_arg += var_base;
        }

        for (size_t i = begin; i < prog_.size(); i++) {
            at_ = static_cast<int>(i);
            const Instr& ip = prog_[i];
            if (ip.in_op == OpCode::LABEL) {
                if (labels_[ip.in_arg] >= 0) error("duplicate label");
                labels_[ip.in_arg] = static_cast<int>(i);
            } else if (ip.in_op == OpCode::EXPORT) {
                std::string name = reinterpret_cast<const char*>(ip.in_arg);
                if (!symbols_.emplace(name, static_cast<int>(i)).second)
                    error("duplicate symbol '" + name + "'");
            }
        }

        for (size_t i = begin; i < prog_.size(); i++) {
            at_ = static_cast<int>(i);
            const Instr& ip = prog_[i];
            const OpInfo* oi = vm_info(ip.in_op);
            if (oi->arg_type == ArgType::LABEL && labels_[ip.in_arg] < 0)
                error("undefined label");
        }
        label_top_ = std::max(label_top_, base + top);
        var_top_ = std::max(var_top_, var_base + named);
    }

    intptr_t load_module(const std::string& name) {
        if (loaded_.count(name)) return 0;

        std::vector<Instr> mod;
        std::string err;
        LoadStatus st = read_module(name, mod, pool_, err);
        if (st == LoadStatus::NOT_FOUND) return -1;
        if (st == LoadStatus::INVALID) error("load '" + name + "': " + err);

        size_t begin = prog_.size();
        prog_.insert(prog_.end(), mod.begin(), mod.end());
        link_module(begin, label_top_, var_top_);
        loaded_.insert(name);
        return 0;
    }

    static constexpr uintptr_t kWordBits = sizeof(intptr_t) * 8;

    intptr_t alu(OpCode op, intptr_t a, intptr_t b) {
        switch (op) {
            case OpCode::ADD: return a + b;
            case OpCode::SUB: return a - b;
            case OpCode::MUL: return a * b;
            case OpCode::DIV:
            case OpCode::MOD:
                if (b == 0) error("division by zero");
                return (op == OpCode::DIV) ? (a / b) : (a % b);
            case OpCode::EQ:  return a == b;
            case OpCode::NE:  return a != b;
            case OpCode::LT:  return a < b;
            case OpCode::GT:  return a > b;
            case OpCode::LE:  return a <= b;
            case OpCode::GE:  return a >= b;
            case OpCode::ULT: return static_cast<uintptr_t>(a) <  static_cast<uintptr_t>(b);
            case OpCode::ULE: return static_cast<uintptr_t>(a) <= static_cast<uintptr_t>(b);
            case OpCode::UGT: return static_cast<uintptr_t>(a) >  static_cast<uintptr_t>(b);
            case OpCode::UGE: return static_cast<uintptr_t>(a) >= static_cast<uintptr_t>(b);
            case OpCode::AND: return a & b;
            case OpCode::OR:  return a | b;
            case OpCode::XOR: return a ^ b;
            case OpCode::SHL:
                if (static_cast<uintptr_t>(b) >= kWordBits) return 0;
                return static_cast<intptr_t>(static_cast<uintptr_t>(a) << b);
            case OpCode::SHR:
                if (static_cast<uintptr_t>(b) >= kWordBits) return 0;
                return static_cast<intptr_t>(static_cast<uintptr_t>(a) >> b);
            case OpCode::SAR:
                if (static_cast<uintptr_t>(b) >= kWordBits) return a < 0 ? -1 : 0;
                return a >> b;
            default:          return 0;
        }
    }

    void step() {
        at_ = pc_;
        if (pc_ < 0 || static_cast<size_t>(pc_) >= prog_.size())
            error("program counter out of range");
        const Instr& ip = prog_[pc_];
        const OpInfo* oi = vm_info(ip.in_op);

        if (oi == nullptr) error("bad opcode");

        if (trace_) {
            show(pc_);
            stk();
            std::cout << '\n';
        }

        if (sp_ < oi->pop_count) error("stack underflow");
        if (sp_ - oi->pop_count + oi->push_count > STACKMAX) error("stack overflow");

        pc_++;
        intptr_t a, b;
        uint8_t* p;

        switch (ip.in_op) {
            case OpCode::END:
                running_ = false; break;
            case OpCode::HALT:
                running_ = false; status_ = static_cast<int>(ip.in_arg); break;
            case OpCode::NOP:
            case OpCode::LABEL:
                break;

            case OpCode::PUSH:
            case OpCode::PUSHS:
                push(ip.in_arg); break;
            case OpCode::POP:
                pop(); break;
            case OpCode::DUP:
                a = pop(); push(a); push(a); break;
            case OpCode::SWAP:
                b = pop(); a = pop(); push(b); push(a); break;
            case OpCode::OVER:
                b = pop(); a = pop(); push(a); push(b); push(a); break;

            case OpCode::ADD: case OpCode::SUB: case OpCode::MUL: case OpCode::DIV: case OpCode::MOD:
            case OpCode::EQ:  case OpCode::NE:  case OpCode::LT:  case OpCode::GT:  case OpCode::LE:
            case OpCode::GE:  case OpCode::AND: case OpCode::OR:
            case OpCode::XOR: case OpCode::SHL: case OpCode::SHR: case OpCode::SAR:
            case OpCode::ULT: case OpCode::ULE: case OpCode::UGT: case OpCode::UGE:
                b = pop();
                a = pop();
                a = alu(ip.in_op, a, b);
                if (running_) push(a);
                break;
            case OpCode::NEG: push(-pop()); break;
            case OpCode::NOT: push(!pop()); break;
            case OpCode::BNOT: push(~pop()); break;

            case OpCode::LOAD:  push(vars_[ip.in_arg]);  break;
            case OpCode::STORE: vars_[ip.in_arg] = pop(); break;

            case OpCode::JMP: pc_ = labels_[ip.in_arg]; break;
            case OpCode::JZ:  if (pop() == 0) pc_ = labels_[ip.in_arg]; break;
            case OpCode::JNZ: if (pop() != 0) pc_ = labels_[ip.in_arg]; break;

            case OpCode::CALL:
                if (csp_ >= CALLMAX) error("call stack overflow");
                calls_[csp_++] = pc_;
                pc_ = labels_[ip.in_arg];
                break;
            case OpCode::CALLX: {
                const char* name = reinterpret_cast<const char*>(ip.in_arg);
                auto it = symbols_.find(name);
                if (it == symbols_.end()) error(std::string("undefined symbol '") + name + "'");
                if (csp_ >= CALLMAX) error("call stack overflow");
                calls_[csp_++] = pc_;
                pc_ = it->second;
                break;
            }
            case OpCode::EXPORT:
                break;
            case OpCode::LOADMOD: {
                std::string name = reinterpret_cast<const char*>(ip.in_arg);
                push(load_module(name));
                break;
            }
            case OpCode::ARGC:
                push(static_cast<intptr_t>(args_.size()));
                break;
            case OpCode::ARGV: {
                intptr_t i = pop();
                if (i < 0 || static_cast<size_t>(i) >= args_.size())
                    push(0);
                else
                    push(reinterpret_cast<intptr_t>(&args_[static_cast<size_t>(i)][0]));
                break;
            }
            case OpCode::RET:
                if (csp_ == 0) error("return without call");
                pc_ = calls_[--csp_];
                break;

            case OpCode::MKHEAP:
                a = pop();
                if (a < 0) error("bad heap size");
                p = static_cast<uint8_t*>(std::malloc(a));
                if (p != nullptr) heap_++;
                push(reinterpret_cast<intptr_t>(p));
                break;
            case OpCode::RMHEAP:
                a = pop();
                if (a != 0) { std::free(reinterpret_cast<void*>(a)); heap_--; }
                break;

            case OpCode::LOADW:
                a = pop();
                addr(a, 2);
                p = reinterpret_cast<uint8_t*>(a);
                push(static_cast<int16_t>(p[0] | (p[1] << 8)));
                break;
            case OpCode::STOREW:
                b = pop(); a = pop();
                addr(a, 2);
                p = reinterpret_cast<uint8_t*>(a);
                p[0] = static_cast<uint8_t>(b & 0xFF);
                p[1] = static_cast<uint8_t>((b >> 8) & 0xFF);
                break;

            case OpCode::LOADB:
                a = pop();
                addr(a, 1);
                push(reinterpret_cast<uint8_t*>(a)[0]);
                break;
            case OpCode::STOREB:
                b = pop(); a = pop();
                addr(a, 1);
                reinterpret_cast<uint8_t*>(a)[0] = static_cast<uint8_t>(b & 0xFF);
                break;

            case OpCode::PRINTI:  std::cout << pop(); break;
            case OpCode::PRINTC:  std::cout << static_cast<char>(pop()); break;
            case OpCode::PRINTS:
                a = pop(); addr(a, 1);
                std::cout << reinterpret_cast<const char*>(a);
                break;
            case OpCode::PRINTLN:
                a = pop(); addr(a, 1);
                std::cout << reinterpret_cast<const char*>(a) << '\n';
                break;
            case OpCode::NEWLINE: std::cout << '\n'; break;

            case OpCode::READI: {
                intptr_t v = 0;
                bool ok = read_int(v);
                push(ok ? v : 0);
                push(ok ? 1 : 0);
                break;
            }
            case OpCode::READC: {
                int c = std::cin.get();
                if (c == EOF) std::cin.clear();
                push(c == EOF ? -1 : c);
                break;
            }
            case OpCode::READLN:
                b = pop(); a = pop();
                addr(a, 1);
                if (b <= 0) error("bad buffer size");
                push(read_line(reinterpret_cast<uint8_t*>(a), b));
                break;

            case OpCode::OPEN: {
                int flags = sys::RDONLY;
                b = pop(); a = pop();
                addr(a, 1);
                switch (b) {
                    case 0: flags = sys::RDONLY; break;
                    case 1: flags = sys::WRONLY; break;
                    case 2: flags = sys::RDWR; break;
                    default: error("bad open mode");
                }
                push(sys::open(reinterpret_cast<const char*>(a), flags));
                break;
            }
            case OpCode::CLOSE:
                a = pop();
                push(sys::close(static_cast<int>(a)));
                break;
            case OpCode::UNLINK:
                a = pop();
                addr(a, 1);
                push(sys::unlink(reinterpret_cast<const char*>(a)));
                break;
            case OpCode::READ: {
                intptr_t n = pop();
                b = pop(); a = pop();
                addr(b, 1);
                if (n < 0) error("bad count");
                sync_std(static_cast<int>(a));
                push(sys::read(static_cast<int>(a), reinterpret_cast<void*>(b),
                               static_cast<size_t>(n)));
                break;
            }
            case OpCode::WRITE: {
                intptr_t n = pop();
                b = pop(); a = pop();
                addr(b, 1);
                if (n < 0) error("bad count");
                sync_std(static_cast<int>(a));
                push(fd_write(static_cast<int>(a),
                              reinterpret_cast<const uint8_t*>(b), n));
                break;
            }
        }
    }
};

int main(int argc, char* argv[]) {
    bool list = false;
    bool trace = false;
    std::string filename = "p.out";

    int i = 1;
    for (; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-t") {
            trace = true;
        } else if (arg == "-d") {
            list = true;
        } else if (arg == "--") {
            i++;
            break;
        } else if (arg.empty() || arg[0] != '-') {
            break;
        } else {
            std::cerr << "usage: vm [-t] [-d] [prog.bin [args...]]\n";
            return 2;
        }
    }
    if (i < argc) filename = argv[i++];

    VM vm;
    std::string load_err = vm.load_main(filename);
    if (!load_err.empty()) {
        std::cerr << load_err << '\n';
        return 1;
    }

    if (!list) {
        if (vm.accepts_args()) {
            std::vector<std::string> args{filename};
            for (int k = i; k < argc; k++) args.push_back(argv[k]);
            vm.set_args(std::move(args));
        } else if (i < argc) {
            std::cerr << "vm: " << filename
                      << " does not accept arguments (it needs the 'args' directive)\n";
            return 2;
        }
    }

    try {
        vm.set_trace(trace);
        if (list) {
            vm.list();
            return 0;
        }
        return vm.run();
    }
    catch (const std::runtime_error& e) {
        std::cerr << "Fatal Exception: " << e.what() << '\n';
        return 1;
    }
}