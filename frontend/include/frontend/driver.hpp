#pragma once

#include "frontend/ast.hpp"

#include <string>
#include <utility>

namespace frontend {
struct driver_t {
    std::string source;
    ast_t ast;

    ast_node_t make(node_kind_t kind, std::string text, source_range_t range,
                    std::initializer_list<ast_node_t> children = {}) {
        return ast.make(kind, std::move(text), range, children);
    }

    ast_node_t append(ast_node_t parent, ast_node_t child, source_range_t range) {
        ast.append(parent, child);
        parent->data().range = range;
        return parent;
    }

    ast_node_t function_declarator(ast_node_t base, ast_node_t parameters, source_range_t range) {
        // Parenthesized pointer declarators, e.g. int (*f)(int x), are valid.
        // Semantic analysis checks invalid function/array combinations.
        return make(node_kind_t::function_declarator, "()", range, {base, parameters});
    }

    ast_node_t function(ast_node_t type, ast_node_t declarator, ast_node_t body, source_range_t range) {
        auto current = declarator;
        auto nearest = declarator->data().kind;
        while (current->data().kind != node_kind_t::name) {
            nearest = current->data().kind;
            current = (*current->out_begin())->next_node();
        }
        if (nearest != node_kind_t::function_declarator) {
            throw parse_error_t(error_kind_t::syntax, source, declarator->data().range,
                                "function definition requires a function declarator");
        }
        return make(node_kind_t::function, current->data().text, range, {type, declarator, body});
    }
};
} // namespace frontend
