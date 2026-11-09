#pragma once

#include "hir/hir.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <map>
#include <stdexcept>

// A test oracle for HIR behavior. Addresses count logical scalar cells, not ABI bytes.
// This deliberately has no dependency on AST nodes or lowering implementation details.
class hir_evaluator {
    using id_t = hir::id_t;
    using T = frontend::type_kind_t;
    using O = hir::opcode_t;
    using value_t = std::vector<std::int64_t>;
    const hir::translation_unit_t &unit;
    std::vector<std::int64_t> memory{0};
    std::map<id_t, id_t> globals;
    std::map<id_t, id_t> function_addresses;
    std::size_t steps = 0;

    std::size_t size(const hir::type_t &type) const {
        if (type.kind == T::array) return *type.extent * size(*type.element);
        if (type.kind == T::structure) {
            std::size_t n = 0;
            for (const auto &field : unit.structures.at(*type.record).fields) n += size(field.type);
            return n;
        }
        return 1;
    }
    id_t allocate(std::size_t n) {
        const auto p = memory.size(); memory.resize(p + n); return p;
    }
    value_t read(id_t address, std::size_t n) const {
        if (address == 0 || address + n > memory.size()) throw std::runtime_error("invalid test address");
        return {memory.begin() + address, memory.begin() + address + n};
    }
    void write(id_t address, const value_t &value) {
        if (address == 0 || address + value.size() > memory.size()) throw std::runtime_error("invalid test store");
        std::copy(value.begin(), value.end(), memory.begin() + address);
    }
    value_t constant(const hir::constant_t &c) const {
        value_t result(size(c.type));
        if (auto n = std::get_if<std::int64_t>(&c.value)) result[0] = *n;
        else if (auto n = std::get_if<std::uint64_t>(&c.value)) result[0] = std::bit_cast<std::int64_t>(*n);
        else if (auto s = std::get_if<std::string>(&c.value))
            for (id_t i = 0; i < s->size(); ++i) result[i] = static_cast<unsigned char>((*s)[i]);
        else if (auto members = std::get_if<hir::aggregate_constant_t>(&c.value)) {
            std::size_t offset = 0;
            for (const auto &member : *members) {
                const auto value = constant(member);
                std::copy(value.begin(), value.end(), result.begin() + offset);
                offset += value.size();
            }
        }
        return result;
    }
    std::int64_t convert(std::int64_t n, const hir::type_t &type) const {
        if (type.kind == T::boolean) return n != 0;
        unsigned bits = 64; bool uns = false;
        switch (type.kind) {
        case T::character: case T::signed_character: bits = 8; break;
        case T::unsigned_character: bits = 8; uns = true; break;
        case T::short_integer: bits = 16; break;
        case T::unsigned_short: bits = 16; uns = true; break;
        case T::integer: bits = 32; break;
        case T::unsigned_integer: bits = 32; uns = true; break;
        default: break;
        }
        if (bits == 64) return n;
        auto v = static_cast<std::uint64_t>(n) & ((std::uint64_t{1} << bits) - 1);
        if (!uns && (v & (std::uint64_t{1} << (bits - 1)))) v |= ~((std::uint64_t{1} << bits) - 1);
        return std::bit_cast<std::int64_t>(v);
    }
    value_t call(id_t object, const std::vector<value_t> &args) {
        const auto found = std::find_if(unit.functions.begin(), unit.functions.end(), [&](const auto &f) { return f.object == object; });
        if (found == unit.functions.end()) throw std::runtime_error("external function in test evaluator");
        const auto &f = *found;
        auto addresses = globals;
        for (auto id : f.locals) addresses[id] = allocate(size(unit.objects.at(id).type));
        for (id_t i = 0; i < args.size(); ++i) write(addresses.at(f.parameters.at(i)), args[i]);
        std::map<id_t, id_t> labels;
        for (id_t i = 0; i < f.operations.size(); ++i)
            if (f.operations[i].opcode == O::label) labels[f.operations[i].operands[0].id] = i;
        auto get = [&](id_t id) { return read(addresses.at(id), size(unit.objects.at(id).type)); };
        auto put = [&](id_t id, value_t value) { write(addresses.at(id), value); };
        auto scalar = [&](id_t id) { return get(id).at(0); };
        for (id_t pc = 0; pc < f.operations.size(); ++pc) {
            if (++steps > 100000) throw std::runtime_error("test execution step limit");
            const auto &op = f.operations[pc];
            const auto a = op.operands[0].id, b = op.operands[1].id, c = op.operands[2].id;
            switch (op.opcode) {
            case O::label: break;
            case O::constant: put(a, constant(unit.constants.at(b))); break;
            case O::convert: put(a, {convert(scalar(b), unit.objects.at(a).type)}); break;
            case O::address: put(a, {static_cast<std::int64_t>(addresses.at(b))}); break;
            case O::load: put(a, read(static_cast<id_t>(scalar(b)), size(unit.objects.at(a).type))); break;
            case O::store: write(static_cast<id_t>(scalar(a)), get(b)); break;
            case O::element_address: case O::pointer_offset: {
                const auto &pointed = *unit.objects.at(b).type.element;
                const auto stride = size(op.opcode == O::element_address ? *pointed.element : pointed);
                put(a, {scalar(b) + scalar(c) * static_cast<std::int64_t>(stride)}); break;
            }
            case O::pointer_difference:
                put(a, {(scalar(b) - scalar(c)) / static_cast<std::int64_t>(size(*unit.objects.at(b).type.element))}); break;
            case O::field_address: {
                std::size_t offset = 0;
                const auto &fields = unit.structures.at(*unit.objects.at(b).type.element->record).fields;
                for (id_t i = 0; i < c; ++i) offset += size(fields.at(i).type);
                put(a, {scalar(b) + static_cast<std::int64_t>(offset)}); break;
            }
            case O::jump: pc = labels.at(a); break;
            case O::branch: pc = labels.at(scalar(a) ? b : c); break;
            case O::return_value: return op.operands[0].kind == hir::operand_kind_t::none ? value_t{} : get(a);
            case O::unreachable: throw std::runtime_error("executed unreachable");
            case O::call: case O::call_indirect: {
                std::vector<value_t> values;
                for (auto id : op.arguments) values.push_back(get(id));
                auto ret = call(op.opcode == O::call ? a : function_addresses.at(static_cast<id_t>(scalar(a))), values);
                if (op.operands[1].kind != hir::operand_kind_t::none) put(b, std::move(ret));
                break;
            }
            default: {
                auto x = scalar(b);
                auto y = op.operands[2].kind == hir::operand_kind_t::object ? scalar(c) : 0;
                const auto kind = unit.objects.at(b).type.kind;
                const bool uns = kind == T::unsigned_character || kind == T::unsigned_short ||
                    kind == T::unsigned_integer || kind == T::unsigned_long || kind == T::unsigned_long_long || kind == T::pointer;
                const auto ux = static_cast<std::uint64_t>(x), uy = static_cast<std::uint64_t>(y);
                std::uint64_t r = 0;
                if ((op.opcode == O::divide || op.opcode == O::remainder) &&
                    (y == 0 || (!uns && x == std::numeric_limits<std::int64_t>::min() && y == -1)))
                    throw std::runtime_error("undefined test division");
                if ((op.opcode == O::shift_left || op.opcode == O::shift_right) && (y < 0 || y >= 64))
                    throw std::runtime_error("invalid test shift");
                switch (op.opcode) {
                case O::add: r = ux + uy; break; case O::subtract: r = ux - uy; break;
                case O::multiply: r = ux * uy; break; case O::divide: r = uns ? ux / uy : static_cast<std::uint64_t>(x / y); break;
                case O::remainder: r = uns ? ux % uy : static_cast<std::uint64_t>(x % y); break;
                case O::shift_left: r = ux << y; break;
                case O::shift_right: r = uns ? ux >> y : static_cast<std::uint64_t>(x >> y); break;
                case O::bit_and: r = ux & uy; break;
                case O::bit_or: r = ux | uy; break; case O::bit_xor: r = ux ^ uy; break;
                case O::negate: r = std::uint64_t{0} - ux; break; case O::bit_not: r = ~ux; break; case O::logical_not: r = !x; break;
                case O::equal: r = x == y; break; case O::not_equal: r = x != y; break;
                case O::less: r = uns ? ux < uy : x < y; break; case O::less_equal: r = uns ? ux <= uy : x <= y; break;
                case O::greater: r = uns ? ux > uy : x > y; break; case O::greater_equal: r = uns ? ux >= uy : x >= y; break;
                default: throw std::runtime_error("unknown test opcode");
                }
                put(a, {convert(std::bit_cast<std::int64_t>(r), unit.objects.at(a).type)});
            }
            }
        }
        throw std::runtime_error("function fell through");
    }
  public:
    explicit hir_evaluator(const hir::translation_unit_t &u) : unit(u) {
        for (id_t i = 0; i < unit.objects.size(); ++i) {
            const auto &object = unit.objects[i];
            if (object.owner) continue;
            globals[i] = allocate(size(object.type));
            if (object.initializer) write(globals[i], constant(unit.constants.at(*object.initializer)));
            if (object.storage == hir::storage_t::function) function_addresses[globals[i]] = i;
        }
    }
    std::int64_t run() {
        if (unit.initializer) call(unit.functions.at(*unit.initializer).object, {});
        for (const auto &f : unit.functions) if (unit.objects.at(f.object).name == "main") return call(f.object, {}).at(0);
        throw std::runtime_error("no main in test");
    }
};
