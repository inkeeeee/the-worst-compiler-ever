#pragma once

#include "hir/hir.hpp"

#include <string_view>

namespace lir {
using id_t = std::size_t;
using reg_t = std::size_t;
inline constexpr reg_t sp = 0, bp = 1;
// Virtual registers start at 2 and belong to one function activation.
// Integers use LP64, pointers are 64-bit, memory is little-endian.
// Writes sign/zero-extend to 64 bits according to the instruction's scalar type.
// Comparisons write 0/1. Unaligned ld/st are supported by this abstract machine.
enum class scalar_t { i8, u8, i16, u16, i32, u32, i64, u64 };
unsigned width(scalar_t type);
bool is_signed(scalar_t type);
scalar_t scalar_type(const hir::type_t &type);

struct layout_t {
    std::uint64_t size = 0, alignment = 1;
    std::vector<std::uint64_t> fields;
};
layout_t layout(const hir::type_t &type, const std::vector<frontend::structure_info_t> &structures);
std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment);

enum class operand_kind_t { none, reg, immediate, symbol, label };
struct operand_t {
    operand_kind_t kind = operand_kind_t::none;
    std::uint64_t value = 0;
    static operand_t reg(reg_t id) { return {operand_kind_t::reg, id}; }
    static operand_t immediate(std::uint64_t bits) { return {operand_kind_t::immediate, bits}; }
    static operand_t symbol(id_t id) { return {operand_kind_t::symbol, id}; }
    static operand_t label(id_t id) { return {operand_kind_t::label, id}; }
    bool operator==(const operand_t &) const = default;
};
enum class opcode_t {
    label,
    mov,
    ld,
    st,
    add,
    sub,
    mul,
    div,
    rem,
    shl,
    shr,
    bit_and,
    bit_or,
    bit_xor,
    neg,
    bit_not,
    eq,
    ne,
    lt,
    le,
    gt,
    ge,
    jump,
    branch,
    call,
    ret,
    trap
};
struct operation_t {
    opcode_t opcode;
    // Scalar width and signedness, including the input type for comparisons.
    scalar_t type = scalar_t::u64;
    // ld: [destination, address, byte offset]; st: [address, value, byte offset].
    // Arithmetic/mov: [destination, input, optional input].
    // call: [symbol or register]; branch: [register, true label, false label].
    std::array<operand_t, 3> operands{};
    bool operator==(const operation_t &) const = default;
};
struct symbol_t {
    std::string name;
    bool function = false;
};
struct data_value_t {
    std::uint64_t offset;
    scalar_t type;
    std::uint64_t bits;
};
struct data_t {
    id_t symbol;
    std::uint64_t size, alignment;
    bool read_only;
    // Omitted bytes are zero. This keeps large zero-filled arrays compact.
    std::vector<data_value_t> values;
};
enum class location_kind_t { reg, stack };
struct location_t {
    id_t object; // Source HIR identity, used only in the storage map.
    std::string name;
    location_kind_t kind;
    reg_t reg = 0;
    std::int64_t offset = 0; // relative to bp
    std::uint64_t size = 0, alignment = 1;
    bool temporary = false;
};
struct function_t {
    id_t symbol;
    std::uint64_t frame_size = 0;
    std::size_t register_count = 2; // includes sp and bp
    std::vector<location_t> locations;
    std::vector<operation_t> operations;
    hir::cfg_t cfg;
};
struct translation_unit_t {
    std::vector<symbol_t> symbols;
    std::vector<data_t> data;
    std::vector<function_t> functions;
    std::optional<id_t> initializer; // function symbol
};
using diagnostic_t = hir::diagnostic_t;

// Internal stack ABI, independent of a host platform ABI:
// caller reserves a 16-aligned area for result followed by aligned arguments;
// call pushes an 8-byte return address; callee saves bp, sets bp=sp, reserves
// its frame. Incoming area starts at bp+16. ret pops the return address.
// Virtual registers survive nested calls and are private to each activation.
translation_unit_t lower(const hir::translation_unit_t &unit);
bool is_terminator(opcode_t opcode);
std::string_view opcode_name(opcode_t opcode);
void build_cfg(function_t &function);
std::vector<diagnostic_t> verify(const translation_unit_t &unit);
void write(const translation_unit_t &unit, std::ostream &output);
void write_cfg_dot(const translation_unit_t &unit, std::ostream &output);
} // namespace lir
