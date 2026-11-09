#include "lir/lir.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace lir {
namespace {
using H = hir::opcode_t;
using O = opcode_t;
using A = operand_t;
using S = scalar_t;
using T = frontend::type_kind_t;
constexpr auto limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
struct call_layout_t {
    std::uint64_t size = 0;
    std::vector<std::uint64_t> arguments;
};

class lowerer_t {
    const hir::translation_unit_t &hir_;
    translation_unit_t unit_;
    std::unordered_map<id_t, id_t> symbols_, constants_;
    std::unordered_map<id_t, id_t> locations_, direct_addresses_;
    std::vector<bool> promoted_;
    id_t current_ = 0, next_label_ = 0;

    function_t &function() { return unit_.functions.at(current_); }
    const hir::type_t &type(id_t object) const { return hir_.objects.at(object).type; }
    layout_t measure(const hir::type_t &t) const { return layout(t, hir_.structures); }
    reg_t reg() { return function().register_count++; }
    void emit(O op, S type = S::u64, A a = {}, A b = {}, A c = {}) {
        function().operations.push_back({op, type, {a, b, c}});
    }
    reg_t compute(O op, S type, A a, A b = {}) {
        const auto result = reg();
        emit(op, type, A::reg(result), a, b);
        return result;
    }
    reg_t offset(reg_t base, std::uint64_t bytes) {
        if (!bytes) return base;
        if (bytes > limit) return compute(O::sub, S::u64, A::reg(base), A::immediate(std::uint64_t{0} - bytes));
        return compute(O::add, S::u64, A::reg(base), A::immediate(bytes));
    }
    reg_t symbol_address(id_t symbol) { return compute(O::mov, S::u64, A::symbol(symbol)); }
    location_t &location(id_t object) { return function().locations.at(locations_.at(object)); }
    reg_t address(id_t object) {
        if (!hir_.objects.at(object).owner) return symbol_address(symbols_.at(object));
        const auto &where = location(object);
        if (where.kind != location_kind_t::stack) throw std::logic_error("LIR: address of a register object");
        return offset(bp, static_cast<std::uint64_t>(where.offset));
    }
    reg_t get(id_t object) {
        if (hir_.objects.at(object).owner && location(object).kind == location_kind_t::reg) return location(object).reg;
        const auto pointer = address(object);
        const auto result = reg();
        emit(O::ld, scalar_type(type(object)), A::reg(result), A::reg(pointer), A::immediate(0));
        return result;
    }
    void put(id_t object, reg_t source) {
        const auto t = scalar_type(type(object));
        if (hir_.objects.at(object).owner && location(object).kind == location_kind_t::reg) {
            const auto target = location(object).reg;
            if (target != source) emit(O::mov, t, A::reg(target), A::reg(source));
        } else {
            const auto pointer = address(object);
            emit(O::st, t, A::reg(pointer), A::reg(source), A::immediate(0));
        }
    }
    reg_t destination(id_t object) {
        return hir_.objects.at(object).owner && location(object).kind == location_kind_t::reg ? location(object).reg
                                                                                              : reg();
    }
    void copy_chunk(reg_t to, reg_t from, std::uint64_t offset, S type) {
        const auto value = reg();
        emit(O::ld, type, A::reg(value), A::reg(from), A::immediate(offset));
        emit(O::st, type, A::reg(to), A::reg(value), A::immediate(offset));
    }
    void copy(reg_t to, reg_t from, std::uint64_t bytes) {
        // A large aggregate is a loop over machine words, never a memcpy opcode
        // or an instruction per array member. HIR value temporaries retain snapshots.
        std::uint64_t copied = 0;
        if (bytes > 64) {
            const auto end = bytes - bytes % 8;
            const auto index = compute(O::mov, S::u64, A::immediate(0));
            const auto test = next_label_++, body = next_label_++, done = next_label_++;
            emit(O::jump, S::u64, A::label(test));
            emit(O::label, S::u64, A::label(test));
            const auto flag = compute(O::lt, S::u64, A::reg(index), A::immediate(end));
            emit(O::branch, S::u64, A::reg(flag), A::label(body), A::label(done));
            emit(O::label, S::u64, A::label(body));
            const auto src = compute(O::add, S::u64, A::reg(from), A::reg(index));
            const auto dst = compute(O::add, S::u64, A::reg(to), A::reg(index));
            copy_chunk(dst, src, 0, S::u64);
            emit(O::add, S::u64, A::reg(index), A::reg(index), A::immediate(8));
            emit(O::jump, S::u64, A::label(test));
            emit(O::label, S::u64, A::label(done));
            copied = end;
        }
        for (const auto t : {S::u64, S::u32, S::u16, S::u8})
            while (bytes - copied >= width(t)) {
                copy_chunk(to, from, copied, t);
                copied += width(t);
            }
    }
    call_layout_t call_layout(const hir::type_t &signature) const {
        call_layout_t result;
        if (signature.element->kind != T::void_type) result.size = measure(*signature.element).size;
        for (const auto &parameter : signature.parameters) {
            const auto info = measure(parameter);
            result.size = align_up(result.size, info.alignment);
            result.arguments.push_back(result.size);
            if (info.size > limit - result.size) throw std::overflow_error("LIR: argument area overflow");
            result.size += info.size;
        }
        result.size = align_up(result.size, 16);
        if (result.size > limit - 16) throw std::overflow_error("LIR: argument offset overflow");
        return result;
    }
    static std::uint64_t scalar_bits(const hir::constant_t &value) {
        if (const auto n = std::get_if<std::int64_t>(&value.value)) return static_cast<std::uint64_t>(*n);
        if (const auto n = std::get_if<std::uint64_t>(&value.value)) return *n;
        return 0;
    }
    void data_values(const hir::constant_t &value, std::uint64_t base, std::vector<data_value_t> &out) const {
        if (std::holds_alternative<std::monostate>(value.value)) return;
        if (value.type.is_scalar()) {
            const auto bits = scalar_bits(value);
            if (bits) out.push_back({base, scalar_type(value.type), bits});
        } else if (const auto bytes = std::get_if<std::string>(&value.value)) {
            for (std::size_t i = 0; i < bytes->size(); ++i)
                if ((*bytes)[i]) out.push_back({base + i, S::u8, static_cast<unsigned char>((*bytes)[i])});
        } else if (const auto members = std::get_if<hir::aggregate_constant_t>(&value.value)) {
            const auto info = measure(value.type);
            const auto stride = value.type.kind == T::array ? measure(*value.type.element).size : 0;
            for (std::size_t i = 0; i < members->size(); ++i)
                data_values((*members)[i], base + (stride ? i * stride : info.fields.at(i)), out);
        }
    }
    id_t add_symbol(std::string name, bool function) {
        const auto id = unit_.symbols.size();
        unit_.symbols.push_back({std::move(name), function});
        return id;
    }
    id_t constant_symbol(id_t constant) {
        if (const auto it = constants_.find(constant); it != constants_.end()) return it->second;
        const auto symbol = add_symbol(".C" + std::to_string(constant), false);
        const auto &value = hir_.constants.at(constant);
        const auto info = measure(value.type);
        data_t data{symbol, info.size, info.alignment, true, {}};
        data_values(value, 0, data.values);
        unit_.data.push_back(std::move(data));
        constants_[constant] = symbol;
        return symbol;
    }
    void storage(const hir::function_t &source) {
        locations_.clear();
        direct_addresses_.clear();
        promoted_.assign(hir_.objects.size(), false);
        std::vector<bool> used(hir_.objects.size());
        for (auto id : source.parameters) used[id] = true;
        for (const auto &op : source.operations) {
            for (const auto a : op.operands)
                if (a.kind == hir::operand_kind_t::object) used[a.id] = true;
            for (auto id : op.arguments) used[id] = true;
            if (op.opcode == H::address) direct_addresses_[op.operands[0].id] = op.operands[1].id;
        }
        for (auto id : source.locals) promoted_[id] = type(id).is_scalar() && !hir_.objects[id].address_taken;
        // HIR also uses address/load/store for ordinary variable access. Eliminate
        // those addresses only if every use is a direct access to that same object.
        for (const auto &op : source.operations) {
            for (std::size_t i = 0; i < op.operands.size(); ++i) {
                const auto a = op.operands[i];
                if (a.kind != hir::operand_kind_t::object || !direct_addresses_.contains(a.id)) continue;
                if (i == 0 && op.opcode == H::address) continue;
                if ((op.opcode == H::load && i == 1) || (op.opcode == H::store && i == 0)) continue;
                promoted_[direct_addresses_.at(a.id)] = false;
            }
            for (auto a : op.arguments)
                if (direct_addresses_.contains(a)) promoted_[direct_addresses_.at(a)] = false;
        }
        for (auto id : source.locals) {
            if (!used[id]) continue;
            if (direct_addresses_.contains(id) && promoted_[direct_addresses_.at(id)]) continue;
            const auto &object = hir_.objects[id];
            const auto info = measure(object.type);
            location_t where{id, object.name, location_kind_t::stack, 0,
                             0,  info.size,   info.alignment,         object.storage == hir::storage_t::temporary};
            if (promoted_[id]) {
                where.kind = location_kind_t::reg;
                where.reg = reg();
            } else {
                auto &frame = function().frame_size;
                if (info.size > limit - frame) throw std::overflow_error("LIR: frame layout overflow");
                frame = align_up(frame + info.size, info.alignment);
                where.offset = -static_cast<std::int64_t>(frame);
            }
            locations_[id] = function().locations.size();
            function().locations.push_back(std::move(where));
        }
        function().frame_size = align_up(function().frame_size, 16);
    }
    std::optional<id_t> promoted_address(id_t address) const {
        const auto it = direct_addresses_.find(address);
        if (it != direct_addresses_.end() && promoted_[it->second]) return it->second;
        return {};
    }
    void prologue(const hir::function_t &source) {
        emit(O::sub, S::u64, A::reg(sp), A::reg(sp), A::immediate(8));
        emit(O::st, S::u64, A::reg(sp), A::reg(bp), A::immediate(0));
        emit(O::mov, S::u64, A::reg(bp), A::reg(sp));
        if (function().frame_size) emit(O::sub, S::u64, A::reg(sp), A::reg(sp), A::immediate(function().frame_size));
        const auto abi = call_layout(type(source.object));
        for (std::size_t i = 0; i < source.parameters.size(); ++i) {
            const auto id = source.parameters[i];
            const auto displacement = 16 + abi.arguments[i];
            if (type(id).is_scalar()) {
                const auto value = destination(id);
                emit(O::ld, scalar_type(type(id)), A::reg(value), A::reg(bp), A::immediate(displacement));
                put(id, value);
            } else {
                const auto from = offset(bp, displacement), to = address(id);
                copy(to, from, measure(type(id)).size);
            }
        }
    }
    void call(const hir::operation_t &op) {
        const auto callee = op.operands[0].id;
        const auto &signature = op.opcode == H::call ? type(callee) : *type(callee).element;
        const auto abi = call_layout(signature);
        const auto target = op.opcode == H::call ? A::symbol(symbols_.at(callee)) : A::reg(get(callee));
        if (abi.size) emit(O::sub, S::u64, A::reg(sp), A::reg(sp), A::immediate(abi.size));
        for (std::size_t i = 0; i < op.arguments.size(); ++i) {
            const auto id = op.arguments[i];
            if (type(id).is_scalar()) {
                const auto value = get(id);
                emit(O::st, scalar_type(type(id)), A::reg(sp), A::reg(value), A::immediate(abi.arguments[i]));
            } else {
                const auto from = address(id), to = offset(sp, abi.arguments[i]);
                copy(to, from, measure(type(id)).size);
            }
        }
        emit(O::call, S::u64, target);
        if (op.operands[1].kind != hir::operand_kind_t::none) {
            const auto id = op.operands[1].id;
            if (type(id).is_scalar()) {
                const auto value = destination(id);
                emit(O::ld, scalar_type(type(id)), A::reg(value), A::reg(sp), A::immediate(0));
                put(id, value);
            } else {
                const auto to = address(id);
                copy(to, sp, measure(type(id)).size);
            }
        }
        if (abi.size) emit(O::add, S::u64, A::reg(sp), A::reg(sp), A::immediate(abi.size));
    }
    void operation(const hir::operation_t &op) {
        const auto a = op.operands[0].id, b = op.operands[1].id, c = op.operands[2].id;
        switch (op.opcode) {
        case H::label:
            emit(O::label, S::u64, A::label(a));
            return;
        case H::jump:
            emit(O::jump, S::u64, A::label(a));
            return;
        case H::branch:
            emit(O::branch, S::u64, A::reg(get(a)), A::label(b), A::label(c));
            return;
        case H::unreachable:
            emit(O::trap);
            return;
        case H::call:
        case H::call_indirect:
            call(op);
            return;
        case H::return_value:
            if (op.operands[0].kind != hir::operand_kind_t::none) {
                if (type(a).is_scalar()) {
                    const auto value = get(a);
                    emit(O::st, scalar_type(type(a)), A::reg(bp), A::reg(value), A::immediate(16));
                } else {
                    const auto from = address(a), to = offset(bp, 16);
                    copy(to, from, measure(type(a)).size);
                }
            }
            emit(O::mov, S::u64, A::reg(sp), A::reg(bp));
            emit(O::ld, S::u64, A::reg(bp), A::reg(sp), A::immediate(0));
            emit(O::add, S::u64, A::reg(sp), A::reg(sp), A::immediate(8));
            emit(O::ret);
            return;
        case H::address:
            if (!promoted_address(a)) put(a, address(b));
            return;
        case H::constant:
            if (type(a).is_scalar()) {
                const auto value = destination(a);
                emit(O::mov, scalar_type(type(a)), A::reg(value), A::immediate(scalar_bits(hir_.constants.at(b))));
                put(a, value);
            } else {
                const auto from = symbol_address(constant_symbol(b)), to = address(a);
                copy(to, from, measure(type(a)).size);
            }
            return;
        case H::load:
            if (const auto object = promoted_address(b))
                put(a, get(*object));
            else if (type(a).is_scalar()) {
                const auto pointer = get(b), value = destination(a);
                emit(O::ld, scalar_type(type(a)), A::reg(value), A::reg(pointer), A::immediate(0));
                put(a, value);
            } else {
                const auto from = get(b), to = address(a);
                copy(to, from, measure(type(a)).size);
            }
            return;
        case H::store:
            if (const auto object = promoted_address(a))
                put(*object, get(b));
            else if (type(b).is_scalar()) {
                const auto pointer = get(a), value = get(b);
                emit(O::st, scalar_type(type(b)), A::reg(pointer), A::reg(value), A::immediate(0));
            } else {
                const auto from = address(b), to = get(a);
                copy(to, from, measure(type(b)).size);
            }
            return;
        case H::field_address: {
            const auto base = get(b);
            put(a, offset(base, measure(*type(b).element).fields.at(c)));
            return;
        }
        case H::element_address:
        case H::pointer_offset: {
            const auto &element = op.opcode == H::element_address ? *type(b).element->element : *type(b).element;
            const auto base = get(b), index = get(c);
            const auto bytes = compute(O::mul, S::i64, A::reg(index), A::immediate(measure(element).size));
            put(a, compute(O::add, S::u64, A::reg(base), A::reg(bytes)));
            return;
        }
        case H::pointer_difference: {
            const auto left = get(b), right = get(c);
            const auto bytes = compute(O::sub, S::i64, A::reg(left), A::reg(right));
            put(a, compute(O::div, S::i64, A::reg(bytes), A::immediate(measure(*type(b).element).size)));
            return;
        }
        case H::convert: {
            const auto input = get(b), result = destination(a);
            if (type(a).kind == T::boolean)
                emit(O::ne, scalar_type(type(b)), A::reg(result), A::reg(input), A::immediate(0));
            else
                emit(O::mov, scalar_type(type(a)), A::reg(result), A::reg(input));
            put(a, result);
            return;
        }
        default:
            break;
        }
        const auto input = get(b), result = destination(a);
        if (op.opcode == H::negate || op.opcode == H::bit_not || op.opcode == H::logical_not) {
            if (op.opcode == H::logical_not)
                emit(O::eq, scalar_type(type(b)), A::reg(result), A::reg(input), A::immediate(0));
            else
                emit(op.opcode == H::negate ? O::neg : O::bit_not, scalar_type(type(a)), A::reg(result), A::reg(input));
        } else {
            O code;
            switch (op.opcode) {
            case H::add:
                code = O::add;
                break;
            case H::subtract:
                code = O::sub;
                break;
            case H::multiply:
                code = O::mul;
                break;
            case H::divide:
                code = O::div;
                break;
            case H::remainder:
                code = O::rem;
                break;
            case H::shift_left:
                code = O::shl;
                break;
            case H::shift_right:
                code = O::shr;
                break;
            case H::bit_and:
                code = O::bit_and;
                break;
            case H::bit_or:
                code = O::bit_or;
                break;
            case H::bit_xor:
                code = O::bit_xor;
                break;
            case H::equal:
                code = O::eq;
                break;
            case H::not_equal:
                code = O::ne;
                break;
            case H::less:
                code = O::lt;
                break;
            case H::less_equal:
                code = O::le;
                break;
            case H::greater:
                code = O::gt;
                break;
            case H::greater_equal:
                code = O::ge;
                break;
            default:
                throw std::logic_error("LIR: unsupported HIR operation");
            }
            const auto right = get(c);
            emit(code, scalar_type(type(b)), A::reg(result), A::reg(input), A::reg(right));
        }
        put(a, result);
    }

  public:
    explicit lowerer_t(const hir::translation_unit_t &unit) : hir_(unit) {}
    translation_unit_t run() {
        const auto errors = hir::verify(hir_);
        if (!errors.empty()) throw std::invalid_argument("LIR requires valid HIR: " + errors.front().message);
        for (id_t i = 0; i < hir_.objects.size(); ++i) {
            const auto &object = hir_.objects[i];
            if (object.owner) continue;
            const bool function = object.storage == hir::storage_t::function;
            const auto symbol = add_symbol(object.name + "." + std::to_string(i), function);
            symbols_[i] = symbol;
            if (function) continue;
            const auto info = measure(object.type);
            data_t data{symbol, info.size, info.alignment, object.storage == hir::storage_t::string_literal, {}};
            if (object.initializer) data_values(hir_.constants.at(*object.initializer), 0, data.values);
            unit_.data.push_back(std::move(data));
        }
        if (hir_.initializer) unit_.initializer = symbols_.at(hir_.functions.at(*hir_.initializer).object);
        for (const auto &source : hir_.functions) {
            current_ = unit_.functions.size();
            unit_.functions.push_back({symbols_.at(source.object), 0, 2, {}, {}, {}});
            next_label_ = 0;
            for (const auto &op : source.operations)
                for (auto a : op.operands)
                    if (a.kind == hir::operand_kind_t::label) next_label_ = std::max(next_label_, a.id + 1);
            storage(source);
            prologue(source);
            for (const auto &op : source.operations) operation(op);
            build_cfg(function());
        }
        return std::move(unit_);
    }
};
} // namespace
translation_unit_t lower(const hir::translation_unit_t &unit) {
    return lowerer_t(unit).run();
}
} // namespace lir
