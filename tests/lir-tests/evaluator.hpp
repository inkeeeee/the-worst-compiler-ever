#pragma once

#include "lir/lir.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <map>
#include <stdexcept>

// Independent byte-addressed machine for tests. It executes only LIR operations;
// it does not inspect HIR objects, type layouts or the lowering implementation.
class lir_evaluator {
    using O = lir::opcode_t;
    using K = lir::operand_kind_t;
    using S = lir::scalar_t;
    const lir::translation_unit_t &unit_;
    std::vector<std::uint8_t> memory_ = std::vector<std::uint8_t>(8 * 1024 * 1024);
    std::map<lir::id_t, std::uint64_t> addresses_;
    std::map<std::uint64_t, lir::id_t> functions_;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> read_only_;
    std::uint64_t sp_ = memory_.size(), bp_ = 0, calls_ = 0;
    std::size_t steps_ = 0;

    static std::uint64_t normalize(std::uint64_t bits, S type) {
        const auto shift = 64 - lir::width(type) * 8;
        if (!shift) return bits;
        bits &= std::numeric_limits<std::uint64_t>::max() >> shift;
        if (lir::is_signed(type) && (bits & (std::uint64_t{1} << (63 - shift))))
            bits |= std::numeric_limits<std::uint64_t>::max() << (64 - shift);
        return bits;
    }
    void bounds(std::uint64_t address, unsigned bytes) const {
        if (!address || address > memory_.size() || bytes > memory_.size() - address)
            throw std::runtime_error("LIR test: invalid byte address");
    }
    std::uint64_t read(std::uint64_t address, S type) const {
        bounds(address, lir::width(type));
        std::uint64_t value = 0;
        for (unsigned i = 0; i < lir::width(type); ++i) value |= std::uint64_t(memory_[address + i]) << (8 * i);
        return normalize(value, type);
    }
    void write(std::uint64_t address, std::uint64_t value, S type, bool initialize = false) {
        const auto bytes = lir::width(type);
        bounds(address, bytes);
        if (!initialize)
            for (const auto &[begin, end] : read_only_)
                if (address < end && begin < address + bytes)
                    throw std::runtime_error("LIR test: store into readonly data");
        for (unsigned i = 0; i < bytes; ++i) memory_[address + i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
    void invoke(lir::id_t symbol) {
        const auto it = std::find_if(unit_.functions.begin(), unit_.functions.end(),
                                     [&](const auto &f) { return f.symbol == symbol; });
        if (it == unit_.functions.end()) throw std::runtime_error("LIR test: external call");
        const auto &f = *it;
        const auto caller_sp = sp_, caller_bp = bp_, token = ++calls_;
        sp_ -= 8;
        write(sp_, token, S::u64);
        std::vector<std::optional<std::uint64_t>> registers(f.register_count);
        std::map<lir::id_t, std::size_t> labels;
        for (std::size_t i = 0; i < f.operations.size(); ++i)
            if (f.operations[i].opcode == O::label) labels[f.operations[i].operands[0].value] = i;
        auto get = [&](lir::operand_t a) -> std::uint64_t {
            if (a.kind == K::immediate) return a.value;
            if (a.kind == K::symbol) return addresses_.at(a.value);
            if (a.kind != K::reg) throw std::runtime_error("LIR test: invalid value operand");
            if (a.value == lir::sp) return sp_;
            if (a.value == lir::bp) return bp_;
            if (!registers.at(a.value)) throw std::runtime_error("LIR test: undefined register");
            return *registers.at(a.value);
        };
        auto put = [&](lir::operand_t a, std::uint64_t value, S type) {
            value = normalize(value, type);
            if (a.value == lir::sp)
                sp_ = value;
            else if (a.value == lir::bp)
                bp_ = value;
            else
                registers.at(a.value) = value;
        };
        for (std::size_t pc = 0; pc < f.operations.size(); ++pc) {
            if (++steps_ > 4000000) throw std::runtime_error("LIR test: execution step limit");
            const auto &op = f.operations[pc];
            const auto a = op.operands[0], b = op.operands[1], c = op.operands[2];
            switch (op.opcode) {
            case O::label:
                break;
            case O::mov:
                put(a, get(b), op.type);
                break;
            case O::ld:
                put(a, read(get(b) + get(c), op.type), op.type);
                break;
            case O::st:
                write(get(a) + get(c), get(b), op.type);
                break;
            case O::jump:
                pc = labels.at(a.value);
                break;
            case O::branch:
                pc = labels.at(get(a) ? b.value : c.value);
                break;
            case O::call:
                invoke(a.kind == K::symbol ? a.value : functions_.at(get(a)));
                break;
            case O::ret:
                if (read(sp_, S::u64) != token) throw std::runtime_error("LIR test: corrupted return address");
                sp_ += 8;
                if (sp_ != caller_sp || bp_ != caller_bp) throw std::runtime_error("LIR test: unbalanced stack frame");
                return;
            case O::trap:
                throw std::runtime_error("LIR test: executed trap");
            default: {
                const auto x = normalize(get(b), op.type);
                const auto raw_y = c.kind == K::none ? 0 : get(c);
                const auto y = normalize(raw_y, op.type);
                const auto sx = std::bit_cast<std::int64_t>(x), sy = std::bit_cast<std::int64_t>(y);
                const bool signed_op = lir::is_signed(op.type);
                std::uint64_t r = 0;
                bool comparison = false;
                if (op.opcode == O::div || op.opcode == O::rem) {
                    if (!y || (signed_op && sx == std::numeric_limits<std::int64_t>::min() && sy == -1))
                        throw std::runtime_error("LIR test: undefined division");
                }
                if ((op.opcode == O::shl || op.opcode == O::shr) && raw_y >= lir::width(op.type) * 8)
                    throw std::runtime_error("LIR test: invalid shift");
                switch (op.opcode) {
                case O::add:
                    r = x + y;
                    break;
                case O::sub:
                    r = x - y;
                    break;
                case O::mul:
                    r = x * y;
                    break;
                case O::div:
                    r = signed_op ? static_cast<std::uint64_t>(sx / sy) : x / y;
                    break;
                case O::rem:
                    r = signed_op ? static_cast<std::uint64_t>(sx % sy) : x % y;
                    break;
                case O::shl:
                    r = x << raw_y;
                    break;
                case O::shr:
                    r = signed_op ? static_cast<std::uint64_t>(sx >> raw_y) : x >> raw_y;
                    break;
                case O::bit_and:
                    r = x & y;
                    break;
                case O::bit_or:
                    r = x | y;
                    break;
                case O::bit_xor:
                    r = x ^ y;
                    break;
                case O::neg:
                    r = std::uint64_t{0} - x;
                    break;
                case O::bit_not:
                    r = ~x;
                    break;
                case O::eq:
                    r = x == y;
                    comparison = true;
                    break;
                case O::ne:
                    r = x != y;
                    comparison = true;
                    break;
                case O::lt:
                    r = signed_op ? sx < sy : x < y;
                    comparison = true;
                    break;
                case O::le:
                    r = signed_op ? sx <= sy : x <= y;
                    comparison = true;
                    break;
                case O::gt:
                    r = signed_op ? sx > sy : x > y;
                    comparison = true;
                    break;
                case O::ge:
                    r = signed_op ? sx >= sy : x >= y;
                    comparison = true;
                    break;
                default:
                    throw std::runtime_error("LIR test: unsupported opcode");
                }
                put(a, r, comparison ? S::u8 : op.type);
            }
            }
        }
        throw std::runtime_error("LIR test: function fell through");
    }

  public:
    explicit lir_evaluator(const lir::translation_unit_t &unit) : unit_(unit) {
        std::uint64_t next = 4096;
        for (const auto &data : unit.data) {
            next = (next + data.alignment - 1) & ~(data.alignment - 1);
            if (data.size > memory_.size() / 2 - next) throw std::runtime_error("LIR test: data too large");
            addresses_[data.symbol] = next;
            if (data.read_only) read_only_.emplace_back(next, next + data.size);
            for (const auto &value : data.values) write(next + value.offset, value.bits, value.type, true);
            next += data.size;
        }
        for (lir::id_t i = 0; i < unit.symbols.size(); ++i)
            if (unit.symbols[i].function) {
                const auto address = memory_.size() + 4096 + 16 * i;
                addresses_[i] = address;
                functions_[address] = i;
            }
    }
    std::int64_t run() {
        if (unit_.initializer) invoke(*unit_.initializer);
        for (const auto &f : unit_.functions)
            if (unit_.symbols[f.symbol].name.starts_with("main.")) {
                sp_ -= 16;
                invoke(f.symbol);
                const auto result = std::bit_cast<std::int64_t>(read(sp_, S::i32));
                sp_ += 16;
                if (sp_ != memory_.size() || bp_ != 0) throw std::runtime_error("LIR test: stack not restored");
                return result;
            }
        throw std::runtime_error("LIR test: no main");
    }
};
