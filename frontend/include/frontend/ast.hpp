#pragma once

#include "frontend/source.hpp"
#include "tree.hpp"

#include <initializer_list>
#include <ostream>
#include <string>
#include <string_view>

namespace frontend {
enum class node_kind_t {
    translation_unit, function, declaration, declarators, parameter, parameters,
    type, name, pointer_declarator, array_declarator, function_declarator,
    initializer, initializer_list, block, empty, if_statement, while_statement,
    for_statement, return_statement, break_statement, continue_statement,
    expression_statement, identifier, integer_literal, boolean_literal,
    character_literal, string_literal, unary, binary, assignment,
    call, arguments, subscript, postfix
};

struct node_data_t {
    node_kind_t kind;
    std::string text;
    source_range_t range;
};

using ast_tree_t = tree::tree_t<node_data_t, std::string>;
using ast_node_t = ast_tree_t::node_iterator;

std::string_view node_kind_name(node_kind_t kind);

class ast_t {
  public:
    ast_node_t make(node_kind_t kind, std::string text, source_range_t range,
                    std::initializer_list<ast_node_t> children = {});
    void append(ast_node_t parent, ast_node_t child);
    const ast_tree_t &tree() const noexcept { return tree_; }
    bool check() const;
    void write_dot(std::ostream &output) const;
    void dump(std::ostream &output) const;

  private:
    ast_tree_t tree_;
};
}
