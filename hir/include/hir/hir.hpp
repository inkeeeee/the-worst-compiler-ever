#pragma once

#include "frontend/semantic.hpp"
#include "graph.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

namespace hir {
using id_t = std::size_t;
using type_t = frontend::semantic_type_t;
using range_t = frontend::source_range_t;

enum class storage_t { global, local, parameter, temporary, function, string_literal };
struct object_t {
    std::string name;
    type_t type;
    storage_t storage;
    std::optional<id_t> owner; // function index for locals, parameters and temporaries
    std::optional<id_t> initializer; // constant index for static storage
    bool address_taken = false; // explicit source-level &, not routine HIR loads/stores
};
struct constant_t;
// Members are ordered by array index or struct field; omitted trailing members
// have their type's zero value. Nested constants belong to their parent value.
using aggregate_constant_t = std::vector<constant_t>;
using constant_value_t = std::variant<std::monostate, std::int64_t, std::uint64_t, std::string, aggregate_constant_t>;
struct constant_t {
    type_t type;
    // monostate is a typed zero, including recursively zeroed aggregates.
    constant_value_t value;
};
enum class operand_kind_t { none, object, constant, label, field };
struct operand_t {
    operand_kind_t kind = operand_kind_t::none;
    id_t id = 0;
    static operand_t object(id_t id) { return {operand_kind_t::object, id}; }
    static operand_t constant(id_t id) { return {operand_kind_t::constant, id}; }
    static operand_t label(id_t id) { return {operand_kind_t::label, id}; }
    static operand_t field(id_t id) { return {operand_kind_t::field, id}; }
    bool operator==(const operand_t &) const = default;
};
enum class opcode_t {
    label, constant, convert, address, load, store,
    element_address, field_address, pointer_offset, pointer_difference,
    add, subtract, multiply, divide, remainder, shift_left, shift_right,
    bit_and, bit_or, bit_xor, negate, bit_not, logical_not,
    equal, not_equal, less, less_equal, greater, greater_equal,
    jump, branch, call, call_indirect, return_value, unreachable
};
struct operation_t {
    opcode_t opcode;
    // Value operations: [result, input, input]. Store: [address, value].
    // Call: [callee, optional result], arguments in arguments.
    // Branch: [bool flag, true label, false label]. Return: [optional value].
    std::array<operand_t, 3> operands{};
    std::vector<id_t> arguments;
    range_t range;
};
enum class edge_kind_t { jump, true_branch, false_branch, continuation };
struct basic_block_t {
    id_t label;
    std::size_t begin;
    std::size_t end; // half-open operation range, owned by the function
    bool operator==(const basic_block_t &) const = default;
};
using cfg_t = graph::orgraph_t<basic_block_t, edge_kind_t>;
struct function_t {
    id_t object;
    std::vector<id_t> parameters;
    std::vector<id_t> locals;
    std::vector<operation_t> operations;
    cfg_t cfg;
};
struct translation_unit_t {
    std::vector<frontend::structure_info_t> structures;
    std::vector<object_t> objects;
    std::vector<constant_t> constants;
    std::vector<function_t> functions;
    // Explicit global initializers run in source order before entering main.
    std::optional<id_t> initializer;
};
struct diagnostic_t {
    std::optional<id_t> function;
    std::optional<id_t> operation;
    std::string message;
};

bool is_terminator(opcode_t opcode);
std::string_view opcode_name(opcode_t opcode);
// Normalizes labels and fallthrough jumps, then rebuilds the CFG. Calls end blocks.
void build_cfg(function_t &function);
std::vector<diagnostic_t> verify(const translation_unit_t &unit);
translation_unit_t lower(const frontend::ast_t &ast, const frontend::semantic_result_t &semantics);
void write(const translation_unit_t &unit, std::ostream &output);
void write_cfg_dot(const translation_unit_t &unit, std::ostream &output);
} // namespace hir
