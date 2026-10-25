#include "frontend/semantic.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace frontend {
using T = type_kind_t;
using K = node_kind_t;
namespace {
semantic_type_t primitive(T kind) {
    semantic_type_t type;
    type.kind = kind;
    return type;
}
bool is_error(const semantic_type_t &type) { return type.kind == T::error; }
bool is_void(const semantic_type_t &type) { return type.kind == T::void_type; }
bool is_object(const semantic_type_t &type) {
    return !is_error(type) && !is_void(type) && type.kind != T::function;
}
bool complete_object(const semantic_type_t &type) {
    return is_object(type) && (type.kind != T::array || (type.extent && complete_object(*type.element)));
}
semantic_type_t decay(const semantic_type_t &type) {
    if (type.kind == T::array) return semantic_type_t::pointer_to(*type.element);
    if (type.kind == T::function) return semantic_type_t::pointer_to(type);
    return type;
}
int rank(T kind) {
    switch (kind) {
    case T::boolean: return 0;
    case T::character: case T::signed_character: case T::unsigned_character: return 1;
    case T::short_integer: case T::unsigned_short: return 2;
    case T::integer: case T::unsigned_integer: return 3;
    case T::long_integer: case T::unsigned_long: return 4;
    case T::long_long_integer: case T::unsigned_long_long: return 5;
    default: return -1;
    }
}
bool is_unsigned(T kind) {
    return kind == T::unsigned_character || kind == T::unsigned_short || kind == T::unsigned_integer ||
           kind == T::unsigned_long || kind == T::unsigned_long_long;
}
T unsigned_version(T kind) {
    if (kind == T::integer) return T::unsigned_integer;
    if (kind == T::long_integer) return T::unsigned_long;
    return T::unsigned_long_long;
}
semantic_type_t promote(const semantic_type_t &type) {
    return rank(type.kind) < 3 ? primitive(T::integer) : type;
}
// The language uses LP64: char=8, short=16, int=32, long=long long=64 bits.
semantic_type_t arithmetic_type(semantic_type_t a, semantic_type_t b) {
    a = promote(a);
    b = promote(b);
    if (is_unsigned(a.kind) == is_unsigned(b.kind)) return rank(a.kind) >= rank(b.kind) ? a : b;
    const auto u = is_unsigned(a.kind) ? a : b;
    const auto s = is_unsigned(a.kind) ? b : a;
    if (rank(u.kind) >= rank(s.kind)) return u;
    if (u.kind == T::unsigned_integer) return s; // 64-bit signed holds every unsigned int
    return primitive(unsigned_version(s.kind));
}
bool compatible(const semantic_type_t &a, const semantic_type_t &b) {
    if (a.kind != b.kind) return false;
    if (a.kind == T::pointer) return compatible(*a.element, *b.element);
    if (a.kind == T::array)
        return compatible(*a.element, *b.element) && (!a.extent || !b.extent || a.extent == b.extent);
    if (a.kind == T::function) {
        if (!compatible(*a.element, *b.element) || a.parameters.size() != b.parameters.size()) return false;
        for (std::size_t i = 0; i < a.parameters.size(); ++i)
            if (!compatible(a.parameters[i], b.parameters[i])) return false;
    }
    return true;
}
bool compatible_pointers(const semantic_type_t &a, const semantic_type_t &b, bool allow_void = true) {
    if (a.kind != T::pointer || b.kind != T::pointer) return false;
    if (compatible(*a.element, *b.element)) return true;
    return allow_void && ((is_void(*a.element) && is_object(*b.element)) ||
                          (is_void(*b.element) && is_object(*a.element)));
}
bool assignable(const semantic_type_t &target, const expression_info_t &value) {
    const auto from = decay(value.type);
    if (is_error(target) || is_error(from)) return true; // suppress follow-up errors
    if (target.is_integer() && from.is_integer()) return true;
    if (target.kind == T::boolean && from.is_scalar()) return true;
    if (target.kind == T::pointer && from.is_integer() && value.constant == 0) return true;
    return compatible_pointers(target, from);
}

using node_t = ast_tree_t::const_node_iterator;
std::vector<node_t> children(node_t node) {
    std::vector<node_t> result;
    for (auto edge = node->out_begin(); edge != node->out_end(); ++edge)
        result.push_back(std::as_const(**edge).next_node());
    return result;
}

// Constant folding is deliberately bounded; no host signed overflow or invalid shift.
std::optional<std::int64_t> fold(const std::string &op, std::int64_t a, std::int64_t b) {
    using L = std::numeric_limits<std::int64_t>;
    if (op == "+") {
        if ((b > 0 && a > L::max() - b) || (b < 0 && a < L::min() - b)) return {};
        return a + b;
    }
    if (op == "-") {
        if ((b < 0 && a > L::max() + b) || (b > 0 && a < L::min() + b)) return {};
        return a - b;
    }
    if (op == "*") {
        if (a == 0 || b == 0) return 0;
        if (a > 0 ? (b > 0 ? a > L::max() / b : b < L::min() / a)
                  : (b > 0 ? a < L::min() / b : a < L::max() / b)) return {};
        return a * b;
    }
    if (op == "/" || op == "%") {
        if (b == 0 || (a == L::min() && b == -1)) return {};
        return op == "/" ? a / b : a % b;
    }
    if (op == "<<" || op == ">>") {
        if (b < 0 || b >= 64 || a < 0) return {};
        if (op == ">>") return a >> b;
        if (a > (L::max() >> b)) return {};
        return a << b;
    }
    if (op == "&") return a & b;
    if (op == "|") return a | b;
    if (op == "^") return a ^ b;
    if (op == "==") return a == b;
    if (op == "!=") return a != b;
    if (op == "<") return a < b;
    if (op == "<=") return a <= b;
    if (op == ">") return a > b;
    if (op == ">=") return a >= b;
    if (op == "&&") return a && b;
    if (op == "||") return a || b;
    return {};
}

class analyzer_t {
    semantic_result_t result_;
    std::vector<std::unordered_map<std::string, std::size_t>> bindings_;
    std::size_t scope_ = 0;
    std::size_t loop_depth_ = 0;
    semantic_type_t return_type_;
    std::unordered_map<const node_data_t *, std::size_t> expression_indices_;

    struct declarator_t {
        node_t name;
        semantic_type_t type;
        std::optional<std::size_t> parameter_scope;
    };

    void error(node_t node, std::string message) {
        result_.diagnostics.push_back({node->data().range, std::move(message)});
    }
    void enter_scope(std::string label, source_range_t range) {
        const auto id = result_.scopes.size();
        result_.scopes.push_back({id, scope_, std::move(label), range});
        bindings_.emplace_back();
        scope_ = id;
    }
    void leave_scope() { scope_ = *result_.scopes[scope_].parent; }
    std::optional<std::size_t> lookup(const std::string &name) const {
        auto current = std::optional<std::size_t>(scope_);
        while (current) {
            const auto found = bindings_[*current].find(name);
            if (found != bindings_[*current].end()) return found->second;
            current = result_.scopes[*current].parent;
        }
        return {};
    }
    std::size_t declare(node_t name, const semantic_type_t &type, symbol_kind_t kind, bool defined = false) {
        const auto &text = name->data().text;
        const auto found = bindings_[scope_].find(text);
        if (found != bindings_[scope_].end()) {
            const auto id = found->second;
            auto &previous = result_.symbols[id];
            if (kind == symbol_kind_t::function && previous.kind == kind && compatible(previous.type, type)) {
                if (defined && previous.defined) error(name, "redefinition of function '" + text + "'");
                previous.defined = previous.defined || defined;
            } else {
                error(name, "conflicting or duplicate declaration of '" + text + "' (previous declaration at " +
                      std::to_string(previous.range.begin.line) + ':' +
                      std::to_string(previous.range.begin.column) + ")");
            }
            return id;
        }
        const auto id = result_.symbols.size();
        bindings_[scope_].emplace(text, id);
        result_.symbols.push_back({id, scope_, text, kind, type, name->data().range, defined});
        return id;
    }
    semantic_type_t base_type(node_t node) const {
        const auto &text = node->data().text;
        if (text == "void") return primitive(T::void_type);
        if (text == "bool") return primitive(T::boolean);
        if (text == "char") return primitive(T::character);
        const bool uns = text.starts_with("unsigned ");
        if (text.find("char") != std::string::npos) return primitive(uns ? T::unsigned_character : T::signed_character);
        if (text.find("short") != std::string::npos) return primitive(uns ? T::unsigned_short : T::short_integer);
        if (text.find("long long") != std::string::npos) return primitive(uns ? T::unsigned_long_long : T::long_long_integer);
        if (text.find("long") != std::string::npos) return primitive(uns ? T::unsigned_long : T::long_integer);
        return primitive(uns ? T::unsigned_integer : T::integer);
    }
    declarator_t declarator(node_t node, semantic_type_t type) {
        const auto parts = children(node);
        switch (node->data().kind) {
        case K::name: return {node, std::move(type), {}};
        case K::pointer_declarator:
            return declarator(parts.at(0), semantic_type_t::pointer_to(std::move(type)));
        case K::array_declarator: {
            std::optional<std::size_t> size;
            if (parts.at(1)->data().kind != K::empty) {
                const auto value = expression(parts[1]);
                if (!is_error(value.type) && (!value.type.is_integer() || !value.constant || *value.constant <= 0))
                    error(parts[1], "array size must be a positive integer constant expression");
                else if (value.constant && *value.constant > 0)
                    size = static_cast<std::size_t>(*value.constant);
            }
            if (!is_error(type) && !complete_object(type)) error(node, "array element must have a complete object type");
            return declarator(parts[0], semantic_type_t::array_of(std::move(type), size));
        }
        case K::function_declarator: {
            if (type.kind == T::array || type.kind == T::function) error(node, "function cannot return an array or function");
            // A prototype has its own scope; its parameters never leak into the enclosing scope.
            enter_scope("prototype", node->data().range);
            const auto parameter_scope = scope_;
            std::vector<semantic_type_t> types;
            for (auto param : children(parts.at(1))) {
                const auto p = children(param);
                auto info = declarator(p.at(1), base_type(p.at(0)));
                if (is_void(info.type)) error(info.name, "parameter cannot have type void");
                info.type = decay(info.type); // outer array/function parameter becomes a pointer
                declare(info.name, info.type, symbol_kind_t::parameter);
                types.push_back(info.type);
            }
            leave_scope();
            auto info = declarator(parts[0], semantic_type_t::function_returning(std::move(type), std::move(types)));
            info.parameter_scope = parameter_scope;
            return info;
        }
        default: throw std::logic_error("Invalid declarator node");
        }
    }
    void require_assignable(node_t at, const semantic_type_t &target, const expression_info_t &value,
                            const std::string &context) {
        if (!assignable(target, value))
            error(at, context + ": cannot convert " + decay(value.type).str() + " to " + target.str());
    }
    semantic_type_t initialize(const semantic_type_t &type, node_t init) {
        const auto parts = children(init);
        if (type.kind == T::array) {
            if (init->data().kind == K::string_literal && type.element->kind == T::character) {
                const auto value = expression(init);
                const auto count = *value.type.extent;
                if (type.extent && *type.extent + 1 < count) error(init, "string initializer is too long for array");
                return semantic_type_t::array_of(*type.element, type.extent ? type.extent : value.type.extent);
            }
            if (init->data().kind != K::initializer_list) {
                expression(init);
                error(init, "array requires a brace-enclosed initializer or a character string");
                return type;
            }
            if (type.extent && parts.size() > *type.extent) error(init, "too many elements in array initializer");
            for (auto item : parts) initialize(*type.element, item);
            return semantic_type_t::array_of(*type.element, type.extent ? type.extent : std::optional(parts.size()));
        }
        if (init->data().kind == K::initializer_list) {
            if (parts.size() != 1) error(init, "scalar initializer must contain exactly one value");
            for (auto item : parts) initialize(type, item);
        } else {
            require_assignable(init, type, expression(init), "invalid initializer");
        }
        return type;
    }
    void declaration(node_t node) {
        const auto parts = children(node);
        const auto base = base_type(parts.at(0));
        for (auto entry : children(parts.at(1))) {
            const bool initialized = entry->data().kind == K::initializer;
            const auto entry_parts = children(entry);
            auto info = declarator(initialized ? entry_parts.at(0) : entry, base);
            const bool function = info.type.kind == T::function;
            if (is_void(info.type)) error(info.name, "variable cannot have type void");
            const auto id = declare(info.name, info.type, function ? symbol_kind_t::function : symbol_kind_t::variable);
            if (initialized) {
                if (function) error(entry, "function cannot have an initializer");
                const auto completed = initialize(info.type, entry_parts.at(1));
                // Keep the original binding intact after a duplicate declaration.
                if (result_.symbols[id].range.begin.line == info.name->data().range.begin.line &&
                    result_.symbols[id].range.begin.column == info.name->data().range.begin.column)
                    result_.symbols[id].type = completed;
                info.type = completed;
            }
            if (info.type.kind == T::array && !info.type.extent) error(info.name, "array bound is missing and cannot be inferred");
        }
    }
    void function(node_t node) {
        const auto parts = children(node);
        auto info = declarator(parts.at(1), base_type(parts.at(0)));
        declare(info.name, info.type, symbol_kind_t::function, true);
        // Reuse the signature's scope so each parameter has exactly one binding.
        scope_ = *info.parameter_scope;
        result_.scopes[scope_].label = "function " + info.name->data().text;
        result_.scopes[scope_].range = node->data().range;
        return_type_ = *info.type.element;
        // Parameters and the outermost body share a scope.
        for (auto item : children(parts.at(2))) statement(item);
        leave_scope();
    }
    bool modifiable(const expression_info_t &value) const {
        return value.category == value_category_t::lvalue && is_object(value.type) && value.type.kind != T::array;
    }
    bool pointer_arithmetic(const semantic_type_t &type) const {
        return type.kind == T::pointer && complete_object(*type.element);
    }
    semantic_type_t binary_type(node_t node, const std::string &op,
                                const expression_info_t &left, const expression_info_t &right) {
        const auto a = decay(left.type), b = decay(right.type);
        if (is_error(a) || is_error(b)) return {};
        if (op == "&&" || op == "||") {
            if (a.is_scalar() && b.is_scalar()) return primitive(T::boolean);
        } else if (op == "==" || op == "!=") {
            if ((a.is_integer() && b.is_integer()) || compatible_pointers(a, b) ||
                (a.kind == T::pointer && b.is_integer() && right.constant == 0) ||
                (b.kind == T::pointer && a.is_integer() && left.constant == 0)) return primitive(T::boolean);
        } else if (op == "<" || op == "<=" || op == ">" || op == ">=") {
            if ((a.is_integer() && b.is_integer()) ||
                (pointer_arithmetic(a) && compatible_pointers(a, b, false))) return primitive(T::boolean);
        } else if (a.is_integer() && b.is_integer()) {
            if (op == "<<" || op == ">>") return promote(a);
            return arithmetic_type(a, b);
        } else if (op == "+") {
            if (pointer_arithmetic(a) && b.is_integer()) return a;
            if (a.is_integer() && pointer_arithmetic(b)) return b;
        } else if (op == "-") {
            if (pointer_arithmetic(a) && b.is_integer()) return a;
            if (pointer_arithmetic(a) && compatible_pointers(a, b, false)) return primitive(T::long_integer);
        }
        error(node, "invalid operands to '" + op + "': " + a.str() + " and " + b.str());
        return {};
    }
    expression_info_t expression(node_t node) {
        const auto *key = &node->data();
        if (const auto found = expression_indices_.find(key); found != expression_indices_.end())
            return result_.expressions[found->second];
        expression_info_t value{node->data().kind, node->data().text, node->data().range, {},
                                value_category_t::rvalue, {}, {}};
        const auto parts = children(node);
        const auto &op = node->data().text;
        switch (node->data().kind) {
        case K::identifier: {
            value.symbol = lookup(op);
            if (!value.symbol) { error(node, "use of undeclared identifier '" + op + "'"); break; }
            const auto &symbol = result_.symbols[*value.symbol];
            value.type = symbol.type;
            value.category = symbol.kind == symbol_kind_t::function ? value_category_t::function : value_category_t::lvalue;
            break;
        }
        case K::integer_literal: {
            std::int64_t number;
            const auto parsed = std::from_chars(op.data(), op.data() + op.size(), number, 10);
            if (parsed.ec != std::errc{} || parsed.ptr != op.data() + op.size()) error(node, "integer literal is out of range");
            else {
                value.type = primitive(number <= 2147483647 ? T::integer : T::long_integer);
                value.constant = number;
            }
            break;
        }
        case K::boolean_literal:
            value.type = primitive(T::boolean);
            value.constant = op == "true" ? 1 : 0;
            break;
        case K::character_literal: {
            value.type = primitive(T::character);
            if (op[1] != '\\') value.constant = static_cast<unsigned char>(op[1]);
            else {
                const std::string escapes = "abfnrtv0\\\'\"?";
                const std::string values = std::string("\a\b\f\n\r\t\v", 7) + std::string(1, '\0') + "\\\'\"?";
                const auto index = escapes.find(op[2]);
                if (index != std::string::npos) value.constant = static_cast<unsigned char>(values[index]);
            }
            break;
        }
        case K::string_literal: {
            std::size_t length = 1; // terminating NUL
            for (std::size_t i = 1; i + 1 < op.size(); ++i) {
                if (op[i] == '\\') ++i;
                ++length;
            }
            value.type = semantic_type_t::array_of(primitive(T::character), length);
            value.category = value_category_t::lvalue;
            break;
        }
        case K::binary: {
            const auto a = expression(parts.at(0)), b = expression(parts.at(1));
            value.type = binary_type(node, op, a, b);
            // Fold only signed computations whose mathematical result fits the result type.
            if (!is_error(value.type) && a.constant && b.constant && !is_unsigned(decay(a.type).kind) &&
                !is_unsigned(decay(b.type).kind)) {
                value.constant = fold(op, *a.constant, *b.constant);
                if (value.type.kind == T::integer && value.constant &&
                    (*value.constant < -2147483648LL || *value.constant > 2147483647LL)) value.constant.reset();
                if ((op == "<<" || op == ">>") &&
                    (*b.constant < 0 || *b.constant >= (value.type.kind == T::integer ? 32 : 64)))
                    value.constant.reset();
            }
            break;
        }
        case K::assignment: {
            const auto a = expression(parts.at(0)), b = expression(parts.at(1));
            if (!is_error(a.type) && !modifiable(a)) error(parts[0], "left operand of assignment must be a modifiable lvalue");
            if (op == "=") require_assignable(node, a.type, b, "invalid assignment");
            else {
                auto computed = b;
                computed.type = binary_type(node, op.substr(0, op.size() - 1), a, b);
                computed.constant.reset();
                require_assignable(node, a.type, computed, "invalid compound assignment");
            }
            value.type = a.type;
            break;
        }
        case K::unary: case K::postfix: {
            const auto a = expression(parts.at(0));
            const auto type = decay(a.type);
            if (is_error(type)) break;
            if (op == "&") {
                if (a.category != value_category_t::lvalue && a.category != value_category_t::function)
                    error(node, "address-of requires an lvalue or function");
                else value.type = semantic_type_t::pointer_to(a.type);
            } else if (op == "*") {
                if (type.kind != T::pointer || is_void(*type.element)) error(node, "dereference requires a pointer to an object or function");
                else {
                    value.type = *type.element;
                    value.category = value.type.kind == T::function ? value_category_t::function : value_category_t::lvalue;
                }
            } else if (op == "++" || op == "--") {
                if (!modifiable(a) || (!a.type.is_integer() && !pointer_arithmetic(a.type)))
                    error(node, "'" + op + "' requires a modifiable integer or object pointer lvalue");
                else value.type = a.type;
            } else if (op == "!") {
                if (!type.is_scalar()) error(node, "logical negation requires a scalar operand");
                else {
                    value.type = primitive(T::boolean);
                    if (a.constant) value.constant = !*a.constant;
                }
            } else {
                if (!type.is_integer()) error(node, "unary '" + op + "' requires an integer operand");
                else {
                    value.type = promote(type);
                    if (a.constant && !is_unsigned(value.type.kind)) {
                        if (op == "+") value.constant = a.constant;
                        else if (op == "~") value.constant = ~*a.constant;
                        else if (*a.constant != std::numeric_limits<std::int64_t>::min()) value.constant = -*a.constant;
                        if (value.type.kind == T::integer && value.constant &&
                            (*value.constant < -2147483648LL || *value.constant > 2147483647LL)) value.constant.reset();
                    }
                }
            }
            break;
        }
        case K::subscript: {
            const auto a = expression(parts.at(0)), b = expression(parts.at(1));
            auto pointer = decay(a.type), index = decay(b.type);
            if (pointer.is_integer() && index.kind == T::pointer) std::swap(pointer, index);
            if (is_error(pointer) || is_error(index)) break;
            if (!pointer_arithmetic(pointer) || !index.is_integer()) error(node, "subscript requires an object pointer/array and an integer index");
            else { value.type = *pointer.element; value.category = value_category_t::lvalue; }
            break;
        }
        case K::call: {
            const auto callee = expression(parts.at(0));
            auto type = decay(callee.type);
            std::vector<expression_info_t> args;
            const auto arg_nodes = children(parts.at(1));
            for (auto arg : arg_nodes) args.push_back(expression(arg));
            if (is_error(type)) break;
            if (type.kind != T::pointer || type.element->kind != T::function) {
                error(node, "called expression is not a function"); break;
            }
            type = semantic_type_t(*type.element); // copy before releasing the owning pointer
            if (type.parameters.size() != args.size()) error(node, "wrong number of arguments: expected " +
                std::to_string(type.parameters.size()) + ", got " + std::to_string(args.size()));
            for (std::size_t i = 0; i < std::min(args.size(), type.parameters.size()); ++i)
                require_assignable(arg_nodes[i], type.parameters[i], args[i], "argument " + std::to_string(i + 1));
            value.type = *type.element;
            break;
        }
        default: throw std::logic_error("Expected an expression AST node");
        }
        expression_indices_.emplace(key, result_.expressions.size());
        result_.expressions.push_back(value);
        return value;
    }
    void condition(node_t node) {
        if (node->data().kind == K::empty) return;
        const auto type = decay(expression(node).type);
        if (!is_error(type) && !type.is_scalar()) error(node, "condition must have a scalar type");
    }
    void statement(node_t node) {
        const auto parts = children(node);
        switch (node->data().kind) {
        case K::declaration: declaration(node); break;
        case K::block:
            enter_scope("block", node->data().range);
            for (auto item : parts) statement(item);
            leave_scope();
            break;
        case K::empty: break;
        case K::expression_statement: expression(parts.at(0)); break;
        case K::if_statement:
            condition(parts.at(0));
            for (std::size_t i = 1; i < parts.size(); ++i) statement(parts[i]);
            break;
        case K::while_statement:
            condition(parts.at(0));
            ++loop_depth_;
            statement(parts.at(1));
            --loop_depth_;
            break;
        case K::for_statement:
            enter_scope("for", node->data().range);
            if (parts.at(0)->data().kind == K::declaration) declaration(parts[0]);
            else if (parts[0]->data().kind != K::empty) expression(parts[0]);
            condition(parts.at(1));
            if (parts.at(2)->data().kind != K::empty) expression(parts[2]);
            ++loop_depth_;
            statement(parts.at(3));
            --loop_depth_;
            leave_scope();
            break;
        case K::return_statement:
            if (parts.at(0)->data().kind == K::empty) {
                if (!is_void(return_type_)) error(node, "non-void function must return a value");
            } else {
                const auto value = expression(parts[0]);
                if (is_void(return_type_)) error(node, "void function cannot return a value");
                else require_assignable(parts[0], return_type_, value, "invalid return value");
            }
            break;
        case K::break_statement: case K::continue_statement:
            if (loop_depth_ == 0) error(node, std::string(node->data().kind == K::break_statement ? "break" : "continue") +
                                        " is only allowed inside a loop");
            break;
        default: throw std::logic_error("Unexpected statement AST node");
        }
    }

  public:
    semantic_result_t run(const ast_t &ast, std::string source) {
        if (!ast.check()) throw std::invalid_argument("Cannot analyze an invalid AST");
        result_.source = std::move(source);
        result_.scopes.push_back({0, {}, "global", ast.tree().root()->data().range});
        bindings_.emplace_back();
        for (auto node : children(ast.tree().root())) {
            if (node->data().kind == K::function) function(node);
            else declaration(node);
        }
        return std::move(result_);
    }
};
} // namespace

semantic_type_t semantic_type_t::pointer_to(semantic_type_t type) {
    auto result = primitive(T::pointer);
    result.element = std::make_shared<const semantic_type_t>(std::move(type));
    return result;
}
semantic_type_t semantic_type_t::array_of(semantic_type_t type, std::optional<std::size_t> size) {
    auto result = primitive(T::array);
    result.element = std::make_shared<const semantic_type_t>(std::move(type));
    result.extent = size;
    return result;
}
semantic_type_t semantic_type_t::function_returning(semantic_type_t type, std::vector<semantic_type_t> params) {
    auto result = primitive(T::function);
    result.element = std::make_shared<const semantic_type_t>(std::move(type));
    result.parameters = std::move(params);
    return result;
}
bool semantic_type_t::is_integer() const noexcept { return rank(kind) >= 0; }
bool semantic_type_t::is_scalar() const noexcept { return is_integer() || kind == T::pointer; }
bool semantic_type_t::operator==(const semantic_type_t &other) const {
    return kind == other.kind && extent == other.extent && parameters == other.parameters &&
           ((!element && !other.element) || (element && other.element && *element == *other.element));
}
std::string semantic_type_t::str() const {
    switch (kind) {
    case T::error: return "<error>";
    case T::void_type: return "void";
    case T::boolean: return "bool";
    case T::character: return "char";
    case T::signed_character: return "signed char";
    case T::unsigned_character: return "unsigned char";
    case T::short_integer: return "short";
    case T::unsigned_short: return "unsigned short";
    case T::integer: return "int";
    case T::unsigned_integer: return "unsigned int";
    case T::long_integer: return "long";
    case T::unsigned_long: return "unsigned long";
    case T::long_long_integer: return "long long";
    case T::unsigned_long_long: return "unsigned long long";
    case T::pointer: return "pointer(" + element->str() + ")";
    case T::array: return "array[" + (extent ? std::to_string(*extent) : "") + "](" + element->str() + ")";
    case T::function: {
        std::string text = "function(";
        for (std::size_t i = 0; i < parameters.size(); ++i) {
            if (i) text += ", ";
            text += parameters[i].str();
        }
        return text + ") -> " + element->str();
    }
    }
    throw std::logic_error("Unknown semantic type");
}
void semantic_result_t::write_diagnostics(std::ostream &output) const {
    for (const auto &diagnostic : diagnostics)
        output << source << ':' << diagnostic.range.begin.line << ':' << diagnostic.range.begin.column
               << ": semantic error: " << diagnostic.message << '\n';
}
void semantic_result_t::write_symbols(std::ostream &output) const {
    for (const auto &scope : scopes) {
        output << "scope #" << scope.id << ' ' << scope.label;
        if (scope.parent) output << " parent=#" << *scope.parent;
        output << '\n';
        for (const auto &symbol : symbols) {
            if (symbol.scope != scope.id) continue;
            output << "  #" << symbol.id << ' '
                   << (symbol.kind == symbol_kind_t::function ? "function " :
                       symbol.kind == symbol_kind_t::parameter ? "parameter " : "variable ")
                   << symbol.name << " : " << symbol.type.str()
                   << " @" << symbol.range.begin.line << ':' << symbol.range.begin.column << '\n';
        }
    }
    if (!output) throw std::ios_base::failure("Cannot write symbol table");
}
void semantic_result_t::write_types(std::ostream &output) const {
    for (const auto &expression : expressions) {
        output << node_kind_name(expression.kind);
        if (!expression.text.empty()) output << ' ' << expression.text;
        output << " @" << expression.range.begin.line << ':' << expression.range.begin.column
               << " : " << expression.type.str() << ' '
               << (expression.category == value_category_t::lvalue ? "lvalue" :
                   expression.category == value_category_t::function ? "function" : "rvalue");
        if (expression.symbol) output << " symbol=#" << *expression.symbol;
        output << '\n';
    }
    if (!output) throw std::ios_base::failure("Cannot write expression types");
}
semantic_result_t analyze(const ast_t &ast, std::string source) {
    return analyzer_t{}.run(ast, std::move(source));
}
} // namespace frontend
