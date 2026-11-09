#include "lir/lir.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace lir {
namespace {
using T = frontend::type_kind_t;
constexpr auto limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
layout_t measure(const hir::type_t &t, const std::vector<frontend::structure_info_t> &records, unsigned depth) {
    if (depth > 128) throw std::invalid_argument("LIR: recursive or excessively nested layout");
    if (t.is_scalar()) {
        const auto n = width(scalar_type(t));
        return {n, n, {}};
    }
    if (t.kind == T::array) {
        if (!t.element || !t.extent || !*t.extent) throw std::invalid_argument("LIR: incomplete array layout");
        auto element = measure(*t.element, records, depth + 1);
        if (*t.extent > limit / element.size) throw std::overflow_error("LIR: array layout overflow");
        return {element.size * *t.extent, element.alignment, {}};
    }
    if (t.kind == T::structure && t.record && *t.record < records.size()) {
        const auto &record = records[*t.record];
        if (!record.defined || record.fields.empty()) throw std::invalid_argument("LIR: incomplete struct layout");
        layout_t result;
        for (const auto &field : record.fields) {
            const auto member = measure(field.type, records, depth + 1);
            result.size = align_up(result.size, member.alignment);
            result.fields.push_back(result.size);
            if (member.size > limit - result.size) throw std::overflow_error("LIR: struct layout overflow");
            result.size += member.size;
            result.alignment = std::max(result.alignment, member.alignment);
        }
        result.size = align_up(result.size, result.alignment);
        return result;
    }
    throw std::invalid_argument("LIR: type has no memory layout");
}
} // namespace
std::uint64_t align_up(std::uint64_t n, std::uint64_t alignment) {
    if (!alignment || (alignment & (alignment - 1))) throw std::invalid_argument("LIR: invalid alignment");
    if (alignment - 1 > limit || n > limit - (alignment - 1)) throw std::overflow_error("LIR: layout overflow");
    return (n + alignment - 1) & ~(alignment - 1);
}
unsigned width(scalar_t t) {
    switch (t) {
    case scalar_t::i8:
    case scalar_t::u8:
        return 1;
    case scalar_t::i16:
    case scalar_t::u16:
        return 2;
    case scalar_t::i32:
    case scalar_t::u32:
        return 4;
    case scalar_t::i64:
    case scalar_t::u64:
        return 8;
    }
    throw std::invalid_argument("LIR: invalid scalar type");
}
bool is_signed(scalar_t t) {
    return t == scalar_t::i8 || t == scalar_t::i16 || t == scalar_t::i32 || t == scalar_t::i64;
}
scalar_t scalar_type(const hir::type_t &t) {
    switch (t.kind) {
    case T::character:
    case T::signed_character:
        return scalar_t::i8;
    case T::boolean:
    case T::unsigned_character:
        return scalar_t::u8;
    case T::short_integer:
        return scalar_t::i16;
    case T::unsigned_short:
        return scalar_t::u16;
    case T::integer:
        return scalar_t::i32;
    case T::unsigned_integer:
        return scalar_t::u32;
    case T::long_integer:
    case T::long_long_integer:
        return scalar_t::i64;
    case T::unsigned_long:
    case T::unsigned_long_long:
    case T::pointer:
        return scalar_t::u64;
    default:
        throw std::invalid_argument("LIR: aggregate does not fit a scalar register");
    }
}
layout_t layout(const hir::type_t &type, const std::vector<frontend::structure_info_t> &structures) {
    return measure(type, structures, 0);
}
} // namespace lir
