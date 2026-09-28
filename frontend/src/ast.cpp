#include "frontend/ast.hpp"

#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace frontend {
std::string_view node_kind_name(node_kind_t kind) {
    switch (kind) {
    case node_kind_t::translation_unit:
        return "TranslationUnit";
    case node_kind_t::function:
        return "Function";
    case node_kind_t::declaration:
        return "Declaration";
    case node_kind_t::declarators:
        return "Declarators";
    case node_kind_t::parameter:
        return "Parameter";
    case node_kind_t::parameters:
        return "Parameters";
    case node_kind_t::type:
        return "Type";
    case node_kind_t::name:
        return "Name";
    case node_kind_t::pointer_declarator:
        return "PointerDeclarator";
    case node_kind_t::array_declarator:
        return "ArrayDeclarator";
    case node_kind_t::function_declarator:
        return "FunctionDeclarator";
    case node_kind_t::initializer:
        return "Initializer";
    case node_kind_t::initializer_list:
        return "InitializerList";
    case node_kind_t::block:
        return "Block";
    case node_kind_t::empty:
        return "Empty";
    case node_kind_t::if_statement:
        return "If";
    case node_kind_t::while_statement:
        return "While";
    case node_kind_t::for_statement:
        return "For";
    case node_kind_t::return_statement:
        return "Return";
    case node_kind_t::break_statement:
        return "Break";
    case node_kind_t::continue_statement:
        return "Continue";
    case node_kind_t::expression_statement:
        return "ExpressionStatement";
    case node_kind_t::identifier:
        return "Identifier";
    case node_kind_t::integer_literal:
        return "IntegerLiteral";
    case node_kind_t::boolean_literal:
        return "BooleanLiteral";
    case node_kind_t::character_literal:
        return "CharacterLiteral";
    case node_kind_t::string_literal:
        return "StringLiteral";
    case node_kind_t::unary:
        return "Unary";
    case node_kind_t::binary:
        return "Binary";
    case node_kind_t::assignment:
        return "Assignment";
    case node_kind_t::call:
        return "Call";
    case node_kind_t::arguments:
        return "Arguments";
    case node_kind_t::subscript:
        return "Subscript";
    case node_kind_t::postfix:
        return "Postfix";
    }
    throw std::logic_error("Unknown AST node kind");
}

ast_node_t ast_t::make(node_kind_t kind, std::string text, source_range_t range,
                       std::initializer_list<ast_node_t> children) {
    auto node = tree_.new_node(node_data_t{kind, std::move(text), range});
    for (auto child : children) {
        append(node, child);
    }
    return node;
}

void ast_t::append(ast_node_t parent, ast_node_t child) {
    if (parent == child || child->in_begin() != child->in_end()) {
        throw std::logic_error("An AST node must have exactly one parent");
    }
    const auto index = std::distance(parent->out_begin(), parent->out_end());
    tree_.new_edge(std::to_string(index), parent, child);
}

bool ast_t::check() const {
    return tree_.check() && tree_.root() != tree_.end() && tree_.root()->data().kind == node_kind_t::translation_unit;
}

void ast_t::write_dot(std::ostream &output) const {
    if (!check()) {
        throw std::logic_error("Cannot export an invalid AST");
    }
    // export ast to graphviz
    tree_.write_dot(
        output,
        [](const node_data_t &node) {
            std::string label(node_kind_name(node.kind));
            if (!node.text.empty()) {
                label += '\n' + node.text;
            }
            label += '\n' + std::to_string(node.range.begin.line) + ':' + std::to_string(node.range.begin.column);
            return label;
        },
        [](const std::string &role) { return role; });
}

void ast_t::dump(std::ostream &output) const {
    if (!check()) {
        throw std::logic_error("Cannot print an invalid AST");
    }
    // print ast as padding text via stack
    using item_t = std::pair<ast_tree_t::const_node_iterator, std::size_t>;
    std::vector<item_t> pending{{tree_.root(), 0}};
    while (!pending.empty()) {
        const auto [node, depth] = pending.back();
        pending.pop_back();
        const auto &data = node->data();
        output << std::string(depth * 2, ' ') << node_kind_name(data.kind);
        if (!data.text.empty()) {
            output << ' ' << data.text;
        }
        output << " @" << data.range.begin.line << ':' << data.range.begin.column << '\n';
        for (auto edge = node->out_end(); edge != node->out_begin();) {
            --edge;
            pending.emplace_back(std::as_const(**edge).next_node(), depth + 1);
        }
    }
    if (!output) {
        throw std::ios_base::failure("Cannot write AST output");
    }
}
} // namespace frontend
