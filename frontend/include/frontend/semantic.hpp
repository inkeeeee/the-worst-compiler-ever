#pragma once

#include "frontend/ast.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace frontend {
enum class type_kind_t {
    error, void_type, boolean, character, signed_character, unsigned_character,
    short_integer, unsigned_short, integer, unsigned_integer, long_integer,
    unsigned_long, long_long_integer, unsigned_long_long, pointer, array, function
};

// Recursive types preserve the difference between int *a[3] and int (*a)[3].
struct semantic_type_t {
    type_kind_t kind = type_kind_t::error;
    std::shared_ptr<const semantic_type_t> element; // pointee, array element, or return type
    std::optional<std::size_t> extent;
    std::vector<semantic_type_t> parameters;

    static semantic_type_t pointer_to(semantic_type_t type);
    static semantic_type_t array_of(semantic_type_t type, std::optional<std::size_t> size);
    static semantic_type_t function_returning(semantic_type_t type, std::vector<semantic_type_t> params);
    bool is_integer() const noexcept;
    bool is_scalar() const noexcept;
    std::string str() const;
    bool operator==(const semantic_type_t &other) const;
};

enum class symbol_kind_t { variable, parameter, function };
enum class value_category_t { lvalue, rvalue, function };

struct scope_info_t {
    std::size_t id;
    std::optional<std::size_t> parent;
    std::string label;
    source_range_t range;
};

struct symbol_info_t {
    std::size_t id;
    std::size_t scope;
    std::string name;
    symbol_kind_t kind;
    semantic_type_t type;
    source_range_t range;
    bool defined = false;
};

struct expression_info_t {
    node_kind_t kind;
    std::string text;
    source_range_t range;
    semantic_type_t type;
    value_category_t category = value_category_t::rvalue;
    std::optional<std::size_t> symbol; // identifier binding into semantic_result_t::symbols
    std::optional<std::int64_t> constant;
};

struct semantic_diagnostic_t {
    source_range_t range;
    std::string message;
};

struct semantic_result_t {
    std::string source;
    std::vector<scope_info_t> scopes;
    std::vector<symbol_info_t> symbols;
    // One entry per expression, including literals and intermediate values.
    // Entries own their data and remain usable after the AST is destroyed.
    std::vector<expression_info_t> expressions;
    std::vector<semantic_diagnostic_t> diagnostics;

    bool ok() const noexcept { return diagnostics.empty(); }
    void write_diagnostics(std::ostream &output) const;
    void write_symbols(std::ostream &output) const;
    void write_types(std::ostream &output) const;
};

// Analyze scopes and types without changing the AST.
semantic_result_t analyze(const ast_t &ast, std::string source = "<input>");
} // namespace frontend
