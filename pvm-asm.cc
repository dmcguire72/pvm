/*
 * PVM Assembler
 *
 * Copyright (C) 2026 Daniel McGuire
 * Licensed under the Apache 2.0 License.
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include "pvm-common.h"

namespace fs = std::filesystem;

struct AsmInstr {
    OpCode op;
    int64_t num_arg = 0;
    std::string str_arg = "";
};

struct AsmError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void write_le64(std::ostream& os, int64_t v) {
    uint64_t val = static_cast<uint64_t>(v);
    for (int i = 0; i < 8; ++i) {
        os.put(static_cast<char>((val >> (i * 8)) & 0xFF));
    }
}

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (std::string::npos == first) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string unescape(const std::string& s) {
    std::string res;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char c = s[++i];
            switch (c) {
                case 'n': res += '\n'; break;
                case 't': res += '\t'; break;
                case 'r': res += '\r'; break;
                case '\\': res += '\\'; break;
                case '"': res += '"'; break;
                default: res += c; break;
            }
        } else {
            res += s[i];
        }
    }
    return res;
}

std::string strip_comment(const std::string& s) {
    bool in_str = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (in_str) {
            if (c == '\\') ++i;
            else if (c == '"') in_str = false;
        } else if (c == '"') {
            in_str = true;
        } else if (c == ';' || c == '#') {
            return s.substr(0, i);
        }
    }
    return s;
}

std::vector<std::string> split_names(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',' || c == ' ' || c == '\t' || c == '\r') {
            if (!cur.empty()) { out.push_back(lower(cur)); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(lower(cur));
    return out;
}

std::string parse_string(const std::string& operand) {
    if (operand.size() < 2 || operand.front() != '"' || operand.back() != '"')
        throw std::runtime_error("String argument must be enclosed in double quotes");
    return unescape(operand.substr(1, operand.size() - 2));
}

class Assembler {
public:
    std::vector<std::vector<AsmInstr>> code;

    uint8_t flags = 0;

    Assembler() {
        for (const auto& oi : optab) op_map_[oi.name] = oi;
    }

    void assemble_root(const std::string& path) {
        seen_.insert(canon(path));
        unit(path);

        for (auto& unit_code : code) {
            for (auto& i : unit_code) {
                if (i.op != OpCode::CALLX) continue;
                auto it = exports_.find(i.str_arg);
                if (it == exports_.end() || it->second < 0) continue;
                i.op = OpCode::CALL;
                i.num_arg = it->second;
                i.str_arg.clear();
            }
        }
    }

private:
    struct Unit {
        std::string path;
        std::unordered_map<std::string, int> labels;
        std::unordered_set<std::string> defined, externs, exports;
        std::unordered_map<std::string, int> vars;
    };

    std::unordered_map<std::string, OpInfo> op_map_;
    std::unordered_map<std::string, int> exports_;
    std::unordered_set<std::string> seen_;
    int next_label_ = 0;
    int next_var_ = NRAWREGISTERS;

    static std::string canon(const std::string& path) {
        try { return fs::weakly_canonical(path).string(); }
        catch (...) { return path; }
    }

    static AsmError located(const std::string& path, int line, const std::string& msg) {
        return AsmError("Error at " + path + " line " + std::to_string(line) + ": " + msg);
    }

    int label_id(Unit& u, const std::string& name) {
        auto it = u.labels.find(name);
        if (it != u.labels.end()) return it->second;
        if (next_label_ >= NLABELS)
            throw std::runtime_error("Exceeded max labels (" + std::to_string(NLABELS) + ")");
        return u.labels[name] = next_label_++;
    }

    void import_file(Unit& u, const std::string& rel) {
        fs::path p = fs::path(u.path).parent_path() / rel;
        if (!fs::exists(p)) p = rel;
        if (!fs::exists(p)) throw std::runtime_error("import: cannot find '" + rel + "'");
        if (!seen_.insert(canon(p.string())).second) return;

        if (is_binary(p.string()))
            import_binary(p.string());
        else if (p.extension() == ".bin")
            throw std::runtime_error("'" + rel + "' is not a PVM binary");
        else
            unit(p.string());
    }

    static bool is_binary(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        char magic[sizeof(BYTECODE_MAGIC) - 1];
        return in.read(magic, sizeof(magic)) &&
               std::string(magic, sizeof(magic)) == BYTECODE_MAGIC;
    }

    void import_binary(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("import: cannot open '" + path + "'");

        in.ignore(sizeof(BYTECODE_MAGIC) - 1);
        int version = in.get();
        if (version != BYTECODE_VERSION && version != BYTECODE_VERSION_FLAGS)
            throw std::runtime_error("'" + path + "': unsupported bytecode version");
        if (version == BYTECODE_VERSION_FLAGS) {
            int f = in.get();
            if (f == EOF) throw std::runtime_error("'" + path + "': truncated header");
            flags |= static_cast<uint8_t>(f);
        }

        std::vector<AsmInstr> mod;
        while (in.peek() != EOF) {
            int c = in.get();
            AsmInstr inst;
            inst.op = static_cast<OpCode>(c);
            const OpInfo* oi = vm_info(inst.op);
            if (!oi) throw std::runtime_error("'" + path + "': invalid opcode in binary");

            if (oi->arg_type == ArgType::STR) {
                char ch;
                bool terminated = false;
                while (in.get(ch)) {
                    if (ch == '\0') { terminated = true; break; }
                    inst.str_arg += ch;
                }
                if (!terminated) throw std::runtime_error("'" + path + "': unterminated string");
            } else if (oi->arg_type != ArgType::NONE) {
                uint64_t v = 0;
                for (int k = 0; k < 8; ++k) {
                    int b = in.get();
                    if (b == EOF) throw std::runtime_error("'" + path + "': truncated argument");
                    v |= static_cast<uint64_t>(b) << (8 * k);
                }
                inst.num_arg = static_cast<int64_t>(v);
            }
            mod.push_back(std::move(inst));
        }

        int top = 0;
        for (const auto& i : mod) {
            if (vm_info(i.op)->arg_type != ArgType::LABEL) continue;
            if (i.num_arg < 0 || i.num_arg >= NLABELS)
                throw std::runtime_error("'" + path + "': label out of range");
            top = std::max<int>(top, static_cast<int>(i.num_arg) + 1);
        }
        if (next_label_ + top > NLABELS)
            throw std::runtime_error("Exceeded max labels (" + std::to_string(NLABELS) + ")");
        for (auto& i : mod)
            if (vm_info(i.op)->arg_type == ArgType::LABEL) i.num_arg += next_label_;
        next_label_ += top;

        int vtop = NRAWREGISTERS;
        for (const auto& i : mod) {
            if (vm_info(i.op)->arg_type != ArgType::REGISTER) continue;
            if (i.num_arg < 0 || i.num_arg >= NREGISTERS)
                throw std::runtime_error("'" + path + "': variable/register out of range");
            vtop = std::max<int>(vtop, static_cast<int>(i.num_arg) + 1);
        }
        int named = vtop - NRAWREGISTERS;
        if (next_var_ + named > NREGISTERS)
            throw std::runtime_error("Exceeded max registers (" +
                std::to_string(NREGISTERS - NRAWREGISTERS) + " named)");
        for (auto& i : mod)
            if (vm_info(i.op)->arg_type == ArgType::REGISTER && i.num_arg >= NRAWREGISTERS)
                i.num_arg += next_var_ - NRAWREGISTERS;
        next_var_ += named;

        for (size_t k = 0; k < mod.size(); ++k) {
            if (mod[k].op != OpCode::EXPORT) continue;
            if (exports_.count(mod[k].str_arg))
                throw std::runtime_error("Duplicate export '" + mod[k].str_arg + "'");
            bool labelled = k > 0 && mod[k - 1].op == OpCode::LABEL;
            exports_[mod[k].str_arg] = labelled ? static_cast<int>(mod[k - 1].num_arg) : -1;
        }

        code.push_back(std::move(mod));
    }

    AsmInstr make_instruction(Unit& u, const std::string& first,
                              const std::string& operand, bool has_operand) {
        AsmInstr inst;
        OpInfo oi;

        if (first == "load" && !operand.empty() && operand[0] == '"') {
            oi = op_map_.at("loadmod");
        } else {
            auto it = op_map_.find(first);
            if (it == op_map_.end())
                throw std::runtime_error("Unknown instruction '" + first + "'");
            oi = it->second;
        }
        inst.op = oi.op;
        if (oi.arg_type == ArgType::NONE) return inst;

        if (!has_operand || operand.empty())
            throw std::runtime_error("Instruction '" + first + "' expects an argument");

        switch (oi.arg_type) {
            case ArgType::NUM:
                inst.num_arg = std::stoll(operand);
                break;
            case ArgType::REGISTER: {
                std::string name = lower(operand);
                auto vit = u.vars.find(name);
                if (vit != u.vars.end()) {
                    inst.num_arg = vit->second;
                    break;
                }

                std::string digits = (name[0] == 'r') ? name.substr(1) : name;
                bool numeric = !digits.empty() &&
                    std::all_of(digits.begin(), digits.end(),
                                [](unsigned char c) { return std::isdigit(c); });
                if (!numeric)
                    throw std::runtime_error("Unknown variable '" + operand +
                                             "' (declare it with 'var')");
                inst.num_arg = std::stoll(digits);
                if (inst.num_arg >= NRAWREGISTERS)
                    throw std::runtime_error("Register index out of bounds (r0-r" +
                        std::to_string(NRAWREGISTERS - 1) + "); declare variables with 'var'");
                break;
            }
            case ArgType::STR:
                inst.str_arg = parse_string(operand);
                break;
            case ArgType::LABEL:
                if (std::isdigit(static_cast<unsigned char>(operand[0]))) {
                    inst.num_arg = std::stoll(operand);
                    break;
                }
                {
                    std::string name = lower(operand);
                    if (u.externs.count(name)) {
                        if (oi.op != OpCode::CALL)
                            throw std::runtime_error("extern symbol '" + name + "' can only be called");
                        auto ex = exports_.find(name);
                        if (ex != exports_.end() && ex->second >= 0) {
                            inst.num_arg = ex->second;
                        } else {
                            inst.op = OpCode::CALLX;
                            inst.str_arg = name;
                        }
                    } else {
                        inst.num_arg = label_id(u, name);
                    }
                }
                break;
            default: break;
        }
        return inst;
    }

    void unit(const std::string& path) {
        std::ifstream in(path);
        if (!in) throw AsmError("Failed to open input file: " + path);

        const size_t idx = code.size();
        code.emplace_back();

        Unit u;
        u.path = path;

        struct Line { int num; std::string text; };
        std::vector<Line> lines;
        std::string raw;
        int n = 0;
        while (std::getline(in, raw)) {
            ++n;
            std::string t = trim(strip_comment(raw));
            if (!t.empty()) lines.push_back({n, t});
        }

        for (const auto& l : lines) {
            try {
                size_t sp = l.text.find_first_of(" \t");
                std::string first = lower(l.text.substr(0, sp));
                if (first == "args") { flags |= FLAG_ARGS; continue; }
                if (first == "var") {
                    auto names = split_names(sp == std::string::npos ? "" : l.text.substr(sp));
                    if (names.empty())
                        throw std::runtime_error("'var' expects at least one name");
                    for (const auto& nm : names) {
                        bool raw_like = nm[0] == 'r' && nm.size() > 1 &&
                            std::all_of(nm.begin() + 1, nm.end(),
                                        [](unsigned char c) { return std::isdigit(c); });
                        if (raw_like || std::isdigit(static_cast<unsigned char>(nm[0])))
                            throw std::runtime_error("'" + nm + "' looks like a register; choose another name");
                        if (u.vars.count(nm))
                            throw std::runtime_error("Duplicate variable/register '" + nm + "'");
                        if (next_var_ >= NREGISTERS)
                            throw std::runtime_error("Exceeded max registers (" +
                                std::to_string(NREGISTERS - NRAWREGISTERS) + " variables)");
                        u.vars[nm] = next_var_++;
                    }
                    continue;
                }
                if (first != "extern" && first != "export") continue;

                auto names = split_names(sp == std::string::npos ? "" : l.text.substr(sp));
                if (names.empty())
                    throw std::runtime_error("'" + first + "' expects at least one name");
                for (const auto& nm : names) {
                    if (u.externs.count(nm) && first == "export")
                        throw std::runtime_error("'" + nm + "' is both extern and export");
                    if (u.exports.count(nm) && first == "extern")
                        throw std::runtime_error("'" + nm + "' is both extern and export");
                    if (first == "extern") {
                        u.externs.insert(nm);
                    } else if (u.exports.insert(nm).second) {
                        if (exports_.count(nm))
                            throw std::runtime_error("Duplicate export '" + nm + "'");
                        exports_[nm] = label_id(u, nm);
                    }
                }
            } catch (const AsmError&) {
                throw;
            } catch (const std::exception& e) {
                throw located(path, l.num, e.what());
            }
        }

        for (const auto& l : lines) {
            try {
                std::string text = l.text;
                size_t sp = text.find_first_of(" \t");
                std::string first = lower(text.substr(0, sp));

                if (first == "extern" || first == "export" || first == "args" || first == "var") continue;

                if (first == "import") {
                    std::string operand = sp == std::string::npos ? "" : trim(text.substr(sp));
                    import_file(u, parse_string(operand));
                    continue;
                }

                if (first.back() == ':') {
                    std::string name = first.substr(0, first.size() - 1);
                    if (name.empty()) throw std::runtime_error("Empty label name");
                    if (!u.defined.insert(name).second)
                        throw std::runtime_error("Duplicate label '" + name + "'");
                    if (u.externs.count(name))
                        throw std::runtime_error("'" + name + "' is declared extern but defined here");

                    AsmInstr li;
                    li.op = OpCode::LABEL;
                    li.num_arg = label_id(u, name);
                    code[idx].push_back(li);

                    if (u.exports.count(name)) {
                        AsmInstr ex;
                        ex.op = OpCode::EXPORT;
                        ex.str_arg = name;
                        code[idx].push_back(ex);
                    }

                    if (sp == std::string::npos) continue;
                    text = trim(text.substr(sp));
                    if (text.empty()) continue;
                    sp = text.find_first_of(" \t");
                    first = lower(text.substr(0, sp));
                }

                bool has_operand = sp != std::string::npos;
                std::string operand = has_operand ? trim(text.substr(sp)) : "";
                code[idx].push_back(make_instruction(u, first, operand, has_operand));
            } catch (const AsmError&) {
                throw;
            } catch (const std::exception& e) {
                throw located(path, l.num, e.what());
            }
        }

        for (const auto& nm : u.exports) {
            if (!u.defined.count(nm))
                throw AsmError("Error in " + path + ": exported symbol '" + nm + "' is not defined");
        }
        for (const auto& kv : u.labels) {
            if (!u.defined.count(kv.first))
                throw AsmError("Error in " + path + ": undefined label '" + kv.first + "'");
        }
    }
};

struct DceStats {
    size_t instr_before = 0, instr_after = 0;
    size_t labels_before = 0, labels_after = 0;
    size_t vars_before = 0, vars_after = 0;
};

DceStats dce(std::vector<AsmInstr>& in, size_t root_size) {
    DceStats st;
    const size_t n = in.size();
    st.instr_before = n;
    if (n == 0) return st;

    auto is_jump = [](OpCode op) {
        return op == OpCode::JMP || op == OpCode::JZ || op == OpCode::JNZ || op == OpCode::CALL;
    };

    std::unordered_map<int64_t, size_t> label_at;
    std::unordered_map<std::string, size_t> export_at;
    std::unordered_set<int64_t> named_before;
    for (size_t i = 0; i < n; ++i) {
        if (in[i].op == OpCode::LABEL) { label_at[in[i].num_arg] = i; ++st.labels_before; }
        else if (in[i].op == OpCode::EXPORT) export_at[in[i].str_arg] = i;
        if (vm_info(in[i].op)->arg_type == ArgType::REGISTER && in[i].num_arg >= NRAWREGISTERS)
            named_before.insert(in[i].num_arg);
    }
    st.vars_before = named_before.size();

    std::vector<char> live(n, 0);
    std::vector<size_t> work;
    auto mark = [&](size_t i) {
        if (i < n && !live[i]) { live[i] = 1; work.push_back(i); }
    };
    auto mark_label = [&](int64_t id) {
        auto it = label_at.find(id);
        if (it == label_at.end())
            throw std::runtime_error("internal error: jump to undefined label " + std::to_string(id));
        mark(it->second);
    };

    mark(0);
    for (size_t i = 0; i < root_size && i < n; ++i)
        if (in[i].op == OpCode::EXPORT) mark(i);

    while (!work.empty()) {
        size_t i = work.back();
        work.pop_back();
        const AsmInstr& x = in[i];
        switch (x.op) {
            case OpCode::END:
            case OpCode::HALT:
            case OpCode::RET:
                break;
            case OpCode::JMP:
                mark_label(x.num_arg);
                break;
            case OpCode::JZ:
            case OpCode::JNZ:
            case OpCode::CALL:
                mark_label(x.num_arg);
                mark(i + 1);
                break;
            case OpCode::CALLX: {
                auto it = export_at.find(x.str_arg);
                if (it != export_at.end()) mark(it->second);
                mark(i + 1);
                break;
            }
            default:
                mark(i + 1);
                break;
        }
    }

    std::unordered_set<int64_t> needed;
    for (size_t i = 0; i < n; ++i)
        if (live[i] && is_jump(in[i].op)) needed.insert(in[i].num_arg);

    std::vector<AsmInstr> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (!live[i]) continue;
        if (in[i].op == OpCode::LABEL && !needed.count(in[i].num_arg)) continue;
        out.push_back(in[i]);
    }

    std::unordered_map<int64_t, int64_t> new_label;
    std::vector<int64_t> named;
    for (const auto& x : out) {
        if (x.op == OpCode::LABEL) new_label.emplace(x.num_arg, static_cast<int64_t>(new_label.size()));
        if (vm_info(x.op)->arg_type == ArgType::REGISTER && x.num_arg >= NRAWREGISTERS) named.push_back(x.num_arg);
    }
    std::sort(named.begin(), named.end());
    named.erase(std::unique(named.begin(), named.end()), named.end());
    std::unordered_map<int64_t, int64_t> new_var;
    for (size_t k = 0; k < named.size(); ++k) new_var[named[k]] = NRAWREGISTERS + static_cast<int64_t>(k);

    for (auto& x : out) {
        const ArgType t = vm_info(x.op)->arg_type;
        if (t == ArgType::LABEL) x.num_arg = new_label.at(x.num_arg);
        else if (t == ArgType::REGISTER && x.num_arg >= NRAWREGISTERS) x.num_arg = new_var.at(x.num_arg);
    }

    st.instr_after = out.size();
    st.labels_after = new_label.size();
    st.vars_after = named.size();
    in = std::move(out);
    return st;
}

int main(int argc, char** argv) {
    bool optimize = false;
    std::vector<std::string> files;
    for (int k = 1; k < argc; ++k) {
        std::string a = argv[k];
        if (a == "-O") optimize = true;
        else if (a.size() > 1 && a[0] == '-') { files.clear(); break; }
        else files.push_back(a);
    }
    if (files.size() != 2) {
        std::cerr << "Usage: " << argv[0] << " [-O] <input.ps> <output.bin>\n"
                  << "  -O  remove code that can never run (dead code elimination)\n";
        return 1;
    }

    Assembler as;
    try {
        as.assemble_root(files[0]);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    std::vector<AsmInstr> instructions;
    for (const auto& unit_code : as.code)
        instructions.insert(instructions.end(), unit_code.begin(), unit_code.end());
    if (instructions.empty() || instructions.back().op != OpCode::END) {
        AsmInstr e;
        e.op = OpCode::END;
        instructions.push_back(e);
    }

    DceStats dstats;
    if (optimize) {
        try {
            dstats = dce(instructions, as.code.empty() ? 0 : as.code[0].size());
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n";
            return 1;
        }
    }

    std::ofstream out(files[1], std::ios::binary);
    if (!out) {
        std::cerr << "Failed to open output file!\n";
        return 1;
    }

    out.write(BYTECODE_MAGIC, sizeof(BYTECODE_MAGIC) - 1);
    if (as.flags) {
        out.put(static_cast<char>(BYTECODE_VERSION_FLAGS));
        out.put(static_cast<char>(as.flags));
    } else {
        out.put(static_cast<char>(BYTECODE_VERSION));
    }

    for (const auto& inst : instructions) {
        out.put(static_cast<char>(inst.op));
        const OpInfo* oi = vm_info(inst.op);

        if (oi->arg_type == ArgType::STR) {
            for (char c : inst.str_arg) out.put(c);
            out.put('\0');
        } else if (oi->arg_type != ArgType::NONE) {
            write_le64(out, inst.num_arg);
        }
    }

    std::cout << "Successfully assembled to " << files[1] << " (" << instructions.size() << " instructions)\n";
    if (optimize) {
        std::cout << "Optimized: " << dstats.instr_before << " -> " << dstats.instr_after
                  << " instructions (labels " << dstats.labels_before << " -> " << dstats.labels_after
                  << ", variables " << dstats.vars_before << " -> " << dstats.vars_after << ")\n";
    }
    return 0;
}