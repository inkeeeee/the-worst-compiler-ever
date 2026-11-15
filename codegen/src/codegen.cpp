#include "codegen/codegen.hpp"
#include "registers.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace codegen {
namespace {
using O = lir::opcode_t;
using A = lir::operand_t;
using K = lir::operand_kind_t;
using S = lir::scalar_t;
using T = frontend::type_kind_t;

std::string c_type(S type) {
    return std::string(lir::is_signed(type) ? "int" : "uint") + std::to_string(8 * lir::width(type)) + "_t";
}
std::string symbol_name(lir::id_t id) {
    return "vm_s" + std::to_string(id);
}
std::string source_name(const lir::symbol_t &symbol) {
    return symbol.name.substr(0, symbol.name.rfind('.'));
}
std::string label(lir::id_t id) {
    return "L" + std::to_string(id);
}

class emitter_t {
    const lir::translation_unit_t &unit_;
    std::ostringstream code_;
    std::map<std::uint64_t, std::size_t> constants_;
    const lir::function_t *function_ = nullptr;
    allocation_t allocation_;

    template <class... Args> void emit(const std::string &macro, const Args &...args) {
        code_ << "    " << macro << '(';
        [[maybe_unused]] bool first = true;
        ((code_ << (std::exchange(first, false) ? "" : ", ") << args), ...);
        code_ << ");\n";
    }
    void constant(unsigned reg, std::uint64_t bits) {
        const auto it = constants_.try_emplace(bits, constants_.size()).first;
        emit("VM_ADDR", reg, "vm_k" + std::to_string(it->second));
        emit("VM_LD", "uint64_t", reg, reg, 0);
    }
    void stack_adjust(O op, std::uint64_t bytes) {
        if (!bytes) return;
        constant(12, bytes);
        emit(op == O::sub ? "VM_SUB" : "VM_ADD", "uint64_t", machine_sp, machine_sp, 12);
        if (op == O::sub) emit("VM_CHECK_STACK");
    }
    std::int64_t slot(std::size_t index) const {
        return -static_cast<std::int64_t>(function_->frame_size + 8 * (index + 1));
    }
    std::int64_t spill(int place) const {
        return slot(allocation_.saved.size() + static_cast<std::size_t>(-place - 1));
    }
    unsigned input(A a, unsigned scratch) {
        if (a.kind == K::immediate) {
            constant(scratch, a.value);
            return scratch;
        }
        const auto place = allocation_.registers.at(a.value);
        if (place >= 0) return static_cast<unsigned>(place);
        emit("VM_LD", "uint64_t", scratch, machine_bp, spill(place));
        return scratch;
    }
    void save_registers(bool restore) {
        for (std::size_t i = 0; i < allocation_.saved.size(); ++i) {
            if (restore)
                emit("VM_LD", "uint64_t", allocation_.saved[i], machine_bp, slot(i));
            else
                emit("VM_ST", "uint64_t", machine_bp, allocation_.saved[i], slot(i));
        }
    }
    void instruction(const lir::operation_t &op) {
        const auto a = op.operands[0], b = op.operands[1], c = op.operands[2];
        const auto type = c_type(op.type);
        switch (op.opcode) {
        case O::label:
            // A label is a statement prefix, so it deliberately has no semicolon.
            code_ << "    VM_LABEL(" << label(a.value) << ")\n";
            return;
        case O::jump:
            emit("VM_JUMP", label(a.value));
            return;
        case O::branch: {
            const auto condition = input(a, 1);
            emit("VM_BRANCH", condition, label(b.value), label(c.value));
            return;
        }
        case O::call:
            if (a.kind == K::symbol) {
                emit("VM_CALL", symbol_name(a.value));
            } else {
                const auto target = input(a, 1);
                emit("VM_ICALL", target);
            }
            return;
        case O::ret:
            emit("VM_RET");
            return;
        case O::trap:
            emit("VM_TRAP");
            return;
        case O::st: {
            const auto address = input(a, 1), value = input(b, 12);
            emit("VM_ST", type, address, value, std::bit_cast<std::int64_t>(c.value));
            return;
        }
        default:
            break;
        }
        const auto place = allocation_.registers.at(a.value);
        const auto dest = place < 0 ? 0u : static_cast<unsigned>(place);
        if (op.opcode == O::mov && b.kind == K::symbol) {
            emit("VM_ADDR", dest, symbol_name(b.value));
        } else {
            const auto left = input(b, 1);
            if (op.opcode == O::ld) {
                emit("VM_LD", type, dest, left, std::bit_cast<std::int64_t>(c.value));
            } else if (op.opcode == O::mov || op.opcode == O::neg || op.opcode == O::bit_not) {
                emit(op.opcode == O::mov ? "VM_MOV" : op.opcode == O::neg ? "VM_NEG" : "VM_NOT", type, dest, left);
            } else {
                const auto right = input(c, 12);
                std::string macro;
                switch (op.opcode) {
                case O::add:
                    macro = "VM_ADD";
                    break;
                case O::sub:
                    macro = "VM_SUB";
                    break;
                case O::mul:
                    macro = "VM_MUL";
                    break;
                case O::div:
                    macro = lir::is_signed(op.type) ? "VM_DIV_S" : "VM_DIV_U";
                    break;
                case O::rem:
                    macro = lir::is_signed(op.type) ? "VM_REM_S" : "VM_REM_U";
                    break;
                case O::shl:
                    macro = "VM_SHL";
                    break;
                case O::shr:
                    macro = "VM_SHR";
                    break;
                case O::bit_and:
                    macro = "VM_AND";
                    break;
                case O::bit_or:
                    macro = "VM_OR";
                    break;
                case O::bit_xor:
                    macro = "VM_XOR";
                    break;
                case O::eq:
                    macro = "VM_EQ";
                    break;
                case O::ne:
                    macro = "VM_NE";
                    break;
                case O::lt:
                    macro = "VM_LT";
                    break;
                case O::le:
                    macro = "VM_LE";
                    break;
                case O::gt:
                    macro = "VM_GT";
                    break;
                case O::ge:
                    macro = "VM_GE";
                    break;
                default:
                    throw std::invalid_argument("C codegen: unsupported LIR operation");
                }
                emit(macro, type, dest, left, right);
            }
        }
        if (place < 0) emit("VM_ST", "uint64_t", machine_bp, dest, spill(place));
        if (a == A::reg(lir::sp)) emit("VM_CHECK_STACK");
    }
    void function(const lir::function_t &f) {
        function_ = &f;
        allocation_ = allocate_registers(f);
        const auto slots = allocation_.saved.size() + allocation_.spill_count;
        const auto limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (f.frame_size > limit || slots > (limit - f.frame_size) / 8)
            throw std::overflow_error("C codegen: spill frame overflow");
        const auto frame = lir::align_up(f.frame_size + 8 * slots, 16);

        // LIR owns the incoming stack ABI. Replace only its canonical allocation;
        // existing local offsets and bp+16 argument/result offsets stay valid.
        std::size_t begin = !f.operations.empty() && f.operations.front().opcode == O::label ? 1 : 0;
        const std::vector<lir::operation_t> prologue = {
            {O::sub, S::u64, {A::reg(lir::sp), A::reg(lir::sp), A::immediate(8)}},
            {O::st, S::u64, {A::reg(lir::sp), A::reg(lir::bp), A::immediate(0)}},
            {O::mov, S::u64, {A::reg(lir::bp), A::reg(lir::sp)}}};
        if (f.operations.size() < begin + prologue.size() ||
            !std::equal(prologue.begin(), prologue.end(), f.operations.begin() + begin))
            throw std::invalid_argument("C codegen: expected the LIR stack prologue");
        auto body = begin + prologue.size();
        if (f.frame_size) {
            const lir::operation_t reserve{
                O::sub, S::u64, {A::reg(lir::sp), A::reg(lir::sp), A::immediate(f.frame_size)}};
            if (body == f.operations.size() || f.operations[body] != reserve)
                throw std::invalid_argument("C codegen: missing LIR frame allocation");
            ++body;
        }
        code_ << "\n/* " << unit_.symbols.at(f.symbol).name << ": frame=" << frame
              << ", spills=" << allocation_.spill_count << " */\nstatic void " << symbol_name(f.symbol) << "(void) {\n";
        for (std::size_t i = 0; i < begin + prologue.size(); ++i) instruction(f.operations[i]);
        stack_adjust(O::sub, frame);
        save_registers(false);
        const lir::operation_t leave{O::mov, S::u64, {A::reg(lir::sp), A::reg(lir::bp)}};
        for (std::size_t i = body; i < f.operations.size(); ++i) {
            if (f.operations[i] == leave) save_registers(true);
            instruction(f.operations[i]);
        }
        code_ << "}\n";
    }
    const hir::type_t &signature(lir::id_t id) const {
        const auto &s = unit_.symbols.at(id).signature;
        if (!s || s->kind != T::function || !s->element)
            throw std::invalid_argument("C codegen: missing signature for " + unit_.symbols.at(id).name);
        return *s;
    }
    void external(lir::id_t id) {
        const auto name = source_name(unit_.symbols[id]);
        const auto &sig = signature(id);
        const auto &params = sig.parameters;
        const auto result = sig.element->kind;
        const bool integer_result = sig.element->is_integer();
        auto integer = [&](std::size_t i) { return i < params.size() && params[i].is_integer(); };
        auto pointer = [&](std::size_t i) { return i < params.size() && params[i].kind == T::pointer; };
        bool valid = false;
        std::string macro;
        if (name == "open") {
            valid = (params.size() == 2 || params.size() == 3) && pointer(0) && integer(1) &&
                    (params.size() == 2 || integer(2)) && integer_result;
            macro = "VM_OPEN";
        } else if (name == "read" || name == "write") {
            valid = params.size() == 3 && integer(0) && pointer(1) && integer(2) && integer_result;
            macro = name == "read" ? "VM_READ" : "VM_WRITE";
        } else if (name == "close") {
            valid = params.size() == 1 && integer(0) && integer_result;
            macro = "VM_CLOSE";
        } else if (name == "malloc") {
            valid = params.size() == 1 && integer(0) && result == T::pointer;
            macro = "VM_MALLOC";
        } else if (name == "free") {
            valid = params.size() == 1 && pointer(0) && result == T::void_type;
            macro = "VM_FREE";
        } else {
            throw std::invalid_argument("C codegen: unsupported external function '" + name +
                                        "' (available: open, read, write, close, malloc, free)");
        }
        if (!valid) throw std::invalid_argument("C codegen: incompatible declaration of '" + name + "'");

        code_ << "\n/* Host adapter: " << name << " */\nstatic void " << symbol_name(id) << "(void) {\n";
        stack_adjust(O::sub, 8);
        emit("VM_ST", "uint64_t", machine_sp, machine_bp, 0);
        emit("VM_MOV", "uint64_t", machine_bp, machine_sp);
        stack_adjust(O::sub, 16);
        emit("VM_ST", "uint64_t", machine_bp, 2, -8);
        std::uint64_t offset = result == T::void_type ? 0 : lir::width(lir::scalar_type(*sig.element));
        for (std::size_t i = 0; i < params.size(); ++i) {
            const auto type = lir::scalar_type(params[i]);
            offset = lir::align_up(offset, lir::width(type));
            emit("VM_LD", c_type(type), i, machine_bp, 16 + offset);
            offset += lir::width(type);
        }
        if (name == "open" && params.size() == 2) constant(2, 0);
        emit(macro);
        if (result != T::void_type) emit("VM_ST", c_type(lir::scalar_type(*sig.element)), machine_bp, 0, 16);
        emit("VM_LD", "uint64_t", 2, machine_bp, -8);
        emit("VM_MOV", "uint64_t", machine_sp, machine_bp);
        emit("VM_LD", "uint64_t", machine_bp, machine_sp, 0);
        stack_adjust(O::add, 8);
        emit("VM_RET");
        code_ << "}\n";
    }
    void entry(lir::id_t main) {
        const auto &sig = signature(main);
        const auto &p = sig.parameters;
        const bool argv = p.size() == 2 && p[0].kind == T::integer && p[1].kind == T::pointer && p[1].element &&
                          p[1].element->kind == T::pointer && p[1].element->element &&
                          p[1].element->element->kind == T::character;
        if (sig.element->kind != T::integer || (!p.empty() && !argv))
            throw std::invalid_argument("C codegen: main must be int main() or int main(int argc, char **argv)");
        code_ << "\nstatic void vm_entry(void) {\n";
        stack_adjust(O::sub, 16);
        if (argv) {
            emit("VM_ST", "int32_t", machine_sp, 0, 4);
            emit("VM_ST", "uint64_t", machine_sp, 1, 8);
        }
        // Store argc/argv before the initializer can clobber scratch registers.
        if (unit_.initializer) emit("VM_CALL", symbol_name(*unit_.initializer));
        emit("VM_CALL", symbol_name(main));
        emit("VM_LD", "int32_t", 0, machine_sp, 0);
        stack_adjust(O::add, 16);
        emit("VM_HALT");
        code_ << "}\n\nVM_MAIN(vm_entry)\n";
    }

  public:
    explicit emitter_t(const lir::translation_unit_t &unit) : unit_(unit) {}
    void run(std::ostream &output) {
        const auto diagnostics = lir::verify(unit_);
        if (!diagnostics.empty())
            throw std::invalid_argument("C codegen requires valid LIR: " + diagnostics.front().message);
        std::set<lir::id_t> defined, referenced;
        std::optional<lir::id_t> main;
        for (const auto &f : unit_.functions) {
            defined.insert(f.symbol);
            if (source_name(unit_.symbols.at(f.symbol)) == "main") main = f.symbol;
            for (const auto &op : f.operations)
                for (const auto a : op.operands)
                    if (a.kind == K::symbol && unit_.symbols.at(a.value).function) referenced.insert(a.value);
        }
        if (!main) throw std::invalid_argument("C codegen: no main definition");
        for (const auto &f : unit_.functions) function(f);
        for (auto id : referenced)
            if (!defined.contains(id)) external(id);
        entry(*main);

        std::ostringstream result;
        result << "/* Generated from LIR. Build with chibicc -Iruntime file.c -o program. */\n"
                  "#include \"vm.h\"\n\nVM_DEFINE_MACHINE();\n\n";
        for (auto id : defined) result << "static void " << symbol_name(id) << "(void);\n";
        for (auto id : referenced)
            if (!defined.contains(id)) result << "static void " << symbol_name(id) << "(void);\n";
        result << '\n';
        for (const auto &data : unit_.data) {
            result << "/* " << unit_.symbols.at(data.symbol).name << " */\n_Alignas(" << data.alignment << ") static "
                   << (data.read_only ? "const " : "") << "unsigned char " << symbol_name(data.symbol) << '['
                   << data.size << "] = {";
            bool first = true;
            for (const auto &value : data.values)
                for (unsigned i = 0; i < lir::width(value.type); ++i) {
                    const auto byte = (value.bits >> (8 * i)) & 255;
                    if (byte) {
                        result << (first ? "\n" : ",\n") << "    [" << value.offset + i << "] = " << byte;
                        first = false;
                    }
                }
            result << (first ? "0" : "\n") << "};\n";
        }
        for (const auto &[bits, id] : constants_)
            result << "static const uint64_t vm_k" << id << " = UINT64_C(" << bits << ");\n";
        result << code_.str();
        output << result.str();
    }
};
} // namespace
void write_c(const lir::translation_unit_t &unit, std::ostream &output) {
    emitter_t(unit).run(output);
}
} // namespace codegen
