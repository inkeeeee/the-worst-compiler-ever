#include "hir/hir.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace hir {
namespace {
using K = frontend::node_kind_t;
using T = frontend::type_kind_t;
using O = opcode_t;
using A = operand_t;
using node_t = frontend::ast_tree_t::const_node_iterator;
type_t primitive(T kind) { type_t t; t.kind = kind; return t; }
std::vector<node_t> children(node_t node) {
    std::vector<node_t> result;
    for (auto it = node->out_begin(); it != node->out_end(); ++it)
        result.push_back(std::as_const(**it).next_node());
    return result;
}
node_t declared_name(node_t node) {
    while (node->data().kind != K::name) node = children(node).at(0);
    return node;
}
std::string string_bytes(const std::string &text) {
    std::string result;
    const std::string escapes = "abfnrtv0\\\'\"?";
    const std::string values = std::string("\a\b\f\n\r\t\v", 7) + std::string(1, '\0') + "\\\'\"?";
    for (std::size_t i = 1; i + 1 < text.size(); ++i) {
        if (text[i] == '\\') result += values.at(escapes.find(text.at(++i)));
        else result += text[i];
    }
    result += '\0';
    return result;
}

class lowerer_t {
    const frontend::semantic_result_t &sem_;
    translation_unit_t unit_;
    std::unordered_map<id_t, id_t> symbols_;
    std::unordered_map<id_t, id_t> strings_;
    id_t current_ = 0;
    id_t label_ = 0;
    range_t range_;
    std::vector<std::pair<id_t, id_t>> loops_; // break, continue

    const frontend::expression_info_t &info(node_t node) const { return sem_.expression(node->data().id); }
    type_t type(id_t object) const { return unit_.objects.at(object).type; }
    function_t &function() { return unit_.functions.at(current_); }
    id_t object(std::string name, type_t t, storage_t storage, std::optional<id_t> owner = {}) {
        const auto id = unit_.objects.size();
        unit_.objects.push_back({std::move(name), std::move(t), storage, owner, {}});
        if (owner) unit_.functions.at(*owner).locals.push_back(id);
        return id;
    }
    id_t temporary(type_t t) {
        return object("t" + std::to_string(unit_.objects.size()), std::move(t), storage_t::temporary, current_);
    }
    void emit(O opcode, A a = {}, A b = {}, A c = {}, std::vector<id_t> args = {}) {
        function().operations.push_back({opcode, {a, b, c}, std::move(args), range_});
    }
    id_t value(O opcode, type_t t, A a = {}, A b = {}) {
        auto result = temporary(std::move(t));
        emit(opcode, A::object(result), a, b);
        return result;
    }
    id_t constant(type_t t, constant_value_t v) {
        const auto id = unit_.constants.size();
        unit_.constants.push_back({t, std::move(v)});
        return value(O::constant, std::move(t), A::constant(id));
    }
    id_t integer(std::int64_t n, T kind = T::long_integer) { return constant(primitive(kind), n); }
    id_t convert(id_t from, const type_t &to) {
        if (type(from) == to) return from;
        return value(O::convert, to, A::object(from));
    }
    id_t address(id_t object_id) {
        return value(O::address, type_t::pointer_to(type(object_id)), A::object(object_id));
    }
    id_t load(id_t pointer) { return value(O::load, *type(pointer).element, A::object(pointer)); }
    void store(id_t pointer, id_t source) {
        source = convert(source, *type(pointer).element);
        emit(O::store, A::object(pointer), A::object(source));
    }
    void label(id_t id) { emit(O::label, A::label(id)); }
    void jump(id_t id) {
        const auto &code = function().operations;
        if (!code.empty() && is_terminator(code.back().opcode) &&
            code.back().opcode != O::call && code.back().opcode != O::call_indirect) return;
        emit(O::jump, A::label(id));
    }
    id_t field_address(id_t base, id_t index) {
        const auto record = *type(base).element->record;
        return value(O::field_address, type_t::pointer_to(unit_.structures.at(record).fields.at(index).type),
                     A::object(base), A::field(index));
    }
    id_t array_element(id_t base, id_t index) {
        return value(O::element_address, type_t::pointer_to(*type(base).element->element),
                     A::object(base), A::object(index));
    }
    id_t symbol_object(id_t symbol_id) {
        if (const auto it = symbols_.find(symbol_id); it != symbols_.end()) return it->second;
        const auto &symbol = sem_.symbols.at(symbol_id);
        auto id = object(symbol.name, symbol.type, symbol.kind == frontend::symbol_kind_t::parameter ?
                         storage_t::parameter : storage_t::local, current_);
        symbols_[symbol_id] = id;
        return id;
    }
    id_t string_object(node_t node) {
        if (const auto it = strings_.find(node->data().id); it != strings_.end()) return it->second;
        const auto t = info(node).type;
        auto id = object("string" + std::to_string(strings_.size()), t, storage_t::string_literal);
        unit_.objects[id].initializer = unit_.constants.size();
        unit_.constants.push_back({t, string_bytes(node->data().text)});
        strings_[node->data().id] = id;
        return id;
    }
    id_t place(node_t node) {
        auto parts = children(node);
        switch (node->data().kind) {
        case K::identifier: return address(symbol_object(*info(node).symbol));
        case K::string_literal: return address(string_object(node));
        case K::unary:
            if (node->data().text == "*") return expression(parts[0]);
            break;
        case K::subscript: {
            auto a = expression(parts[0]);
            auto b = expression(parts[1]);
            if (type(a).is_integer()) std::swap(a, b);
            b = convert(b, primitive(T::long_integer));
            return value(O::pointer_offset, type(a), A::object(a), A::object(b));
        }
        case K::member: case K::indirect_member: {
            id_t base;
            if (node->data().kind == K::indirect_member) base = expression(parts[0]);
            else if (info(parts[0]).category == frontend::value_category_t::lvalue) base = place(parts[0]);
            else base = address(expression(parts[0])); // materialize a returned/copied struct
            return field_address(base, *info(node).field);
        }
        default: break;
        }
        throw std::logic_error("Cannot lower expression as an address");
    }
    id_t read_place(node_t node) {
        auto pointer = place(node);
        if (info(node).type.kind == T::array) return array_element(pointer, integer(0));
        if (info(node).type.kind == T::function) return pointer;
        return load(pointer);
    }
    id_t binary(const std::string &op, id_t a, id_t b, const type_t &result_type) {
        const auto ta = type(a), tb = type(b);
        if ((op == "+" || op == "-") && (ta.kind == T::pointer || tb.kind == T::pointer)) {
            if (ta.kind == T::pointer && tb.kind == T::pointer) {
                b = convert(b, ta);
                return value(O::pointer_difference, result_type, A::object(a), A::object(b));
            }
            if (tb.kind == T::pointer) std::swap(a, b);
            b = convert(b, primitive(T::long_integer));
            if (op == "-") b = value(O::negate, type(b), A::object(b));
            return value(O::pointer_offset, type(a), A::object(a), A::object(b));
        }
        static const std::unordered_map<std::string, O> codes = {
            {"+", O::add}, {"-", O::subtract}, {"*", O::multiply}, {"/", O::divide}, {"%", O::remainder},
            {"<<", O::shift_left}, {">>", O::shift_right}, {"&", O::bit_and}, {"|", O::bit_or}, {"^", O::bit_xor},
            {"==", O::equal}, {"!=", O::not_equal}, {"<", O::less}, {"<=", O::less_equal},
            {">", O::greater}, {">=", O::greater_equal}
        };
        if (ta.kind == T::pointer || tb.kind == T::pointer) {
            const auto common = ta.kind == T::pointer ? ta : tb;
            a = convert(a, common); b = convert(b, common);
        } else if (op == "<<" || op == ">>") {
            a = convert(a, result_type); b = convert(b, primitive(T::long_integer));
        } else {
            const auto common = frontend::common_arithmetic_type(ta, tb);
            a = convert(a, common); b = convert(b, common);
        }
        return value(codes.at(op), result_type, A::object(a), A::object(b));
    }
    id_t logical(node_t node, node_t left, node_t right) {
        const auto boolean = primitive(T::boolean);
        // Both paths write the same private slot; only the selected path runs.
        const auto result = object("$logical", boolean, storage_t::local, current_);
        auto flag = convert(expression(left), boolean);
        const auto result_pointer = address(result);
        const auto rhs = label_++, shortcut = label_++, done = label_++;
        const bool conjunction = node->data().text == "&&";
        emit(O::branch, A::object(flag), A::label(conjunction ? rhs : shortcut), A::label(conjunction ? shortcut : rhs));
        label(shortcut);
        auto short_value = constant(boolean, std::int64_t(conjunction ? 0 : 1));
        store(result_pointer, short_value);
        jump(done);
        label(rhs);
        auto rhs_value = convert(expression(right), boolean);
        store(result_pointer, rhs_value);
        jump(done);
        label(done);
        return load(result_pointer);
    }
    id_t expression(node_t node) {
        const auto old_range = range_;
        struct restore_t { range_t &target; range_t old; ~restore_t() { target = old; } } restore{range_, old_range};
        range_ = node->data().range;
        const auto parts = children(node);
        const auto &op = node->data().text;
        const auto &semantic = info(node);
        switch (node->data().kind) {
        case K::identifier: case K::string_literal: case K::subscript: case K::member: case K::indirect_member:
            return read_place(node);
        case K::integer_literal: case K::character_literal: case K::boolean_literal:
            return constant(semantic.type, *semantic.constant);
        case K::binary: {
            if (op == "&&" || op == "||") return logical(node, parts[0], parts[1]);
            auto a = expression(parts[0]);
            auto b = expression(parts[1]);
            return binary(op, a, b, semantic.type);
        }
        case K::assignment: {
            auto pointer = place(parts[0]);
            id_t result;
            if (op == "=") result = expression(parts[1]);
            else {
                auto old = load(pointer);
                auto rhs = expression(parts[1]);
                auto operation_type = type(old).kind == T::pointer ? type(old) :
                    (op == "<<=" || op == ">>=") ? frontend::common_arithmetic_type(type(old), primitive(T::integer)) :
                    frontend::common_arithmetic_type(type(old), type(rhs));
                result = binary(op.substr(0, op.size() - 1), old, rhs, operation_type);
            }
            result = convert(result, semantic.type);
            store(pointer, result);
            return result;
        }
        case K::unary: case K::postfix: {
            if (op == "&") return place(parts[0]);
            if (op == "*") return read_place(node);
            if (op == "++" || op == "--") {
                auto pointer = place(parts[0]);
                auto old = load(pointer);
                auto one = integer(1, T::integer);
                const auto operation_type = semantic.type.kind == T::pointer ? semantic.type :
                    frontend::common_arithmetic_type(semantic.type, primitive(T::integer));
                auto updated = convert(binary(op == "++" ? "+" : "-", old, one, operation_type), semantic.type);
                store(pointer, updated);
                return node->data().kind == K::postfix ? old : updated;
            }
            auto arg = convert(expression(parts[0]), semantic.type);
            if (op == "+") return arg;
            return value(op == "-" ? O::negate : op == "~" ? O::bit_not : O::logical_not,
                         semantic.type, A::object(arg));
        }
        case K::call: {
            const bool direct = parts[0]->data().kind == K::identifier &&
                info(parts[0]).category == frontend::value_category_t::function;
            auto callee = direct ? symbol_object(*info(parts[0]).symbol) : expression(parts[0]);
            const auto signature = direct ? type(callee) : *type(callee).element;
            std::vector<id_t> arguments;
            const auto arg_nodes = children(parts[1]);
            for (std::size_t i = 0; i < arg_nodes.size(); ++i)
                arguments.push_back(convert(expression(arg_nodes[i]), signature.parameters.at(i)));
            A result;
            if (semantic.type.kind != T::void_type) result = A::object(temporary(semantic.type));
            emit(direct ? O::call : O::call_indirect, A::object(callee), result, {}, std::move(arguments));
            // Void calls have no value. Semantic analysis prevents its use as an operand.
            return result.id;
        }
        default: throw std::logic_error("Unsupported expression during HIR lowering");
        }
    }
    std::optional<constant_t> initializer_constant(const type_t &t, node_t init) const {
        if (t.kind == T::array && init->data().kind == K::string_literal) {
            auto bytes = string_bytes(init->data().text);
            if (bytes.size() > *t.extent) bytes.resize(*t.extent);
            return constant_t{t, std::move(bytes)};
        }
        if (init->data().kind == K::initializer_list) {
            const auto parts = children(init);
            if (t.kind != T::array && t.kind != T::structure)
                return initializer_constant(t, parts.at(0));
            aggregate_constant_t members;
            for (std::size_t i = 0; i < parts.size(); ++i) {
                const auto &member_type = t.kind == T::array ? *t.element : unit_.structures.at(*t.record).fields.at(i).type;
                auto member = initializer_constant(member_type, parts[i]);
                if (!member) return std::nullopt;
                members.push_back(std::move(*member));
            }
            while (!members.empty() && std::holds_alternative<std::monostate>(members.back().value)) members.pop_back();
            if (members.empty()) return constant_t{t, std::monostate{}};
            return constant_t{t, std::move(members)};
        }
        const auto &source = info(init);
        if (!source.constant) return std::nullopt;
        const auto n = *source.constant;
        if (t.kind == T::pointer) {
            if (n == 0) return constant_t{t, std::monostate{}};
            return std::nullopt;
        }
        if (!t.is_integer()) return std::nullopt;
        if (n == 0) return constant_t{t, std::monostate{}};
        if (t.kind == T::boolean) return constant_t{t, std::int64_t{1}};
        // Preserve the language's runtime conversions when a value needs
        // narrowing or unsigned wrapping; only fold representable values.
        std::int64_t minimum = std::numeric_limits<std::int64_t>::min();
        std::uint64_t maximum = std::numeric_limits<std::int64_t>::max();
        switch (t.kind) {
        case T::character: minimum=-128; maximum=source.type == t ? 255 : 127; break;
        case T::signed_character: minimum=-128; maximum=127; break;
        case T::unsigned_character: minimum=0; maximum=255; break;
        case T::short_integer: minimum=-32768; maximum=32767; break;
        case T::unsigned_short: minimum=0; maximum=65535; break;
        case T::integer: minimum=-2147483648LL; maximum=2147483647; break;
        case T::unsigned_integer: minimum=0; maximum=4294967295ULL; break;
        case T::unsigned_long: case T::unsigned_long_long:
            minimum=0; maximum=std::numeric_limits<std::uint64_t>::max(); break;
        default: break;
        }
        if (n < minimum || (n >= 0 && static_cast<std::uint64_t>(n) > maximum)) return std::nullopt;
        return constant_t{t, n};
    }
    void initialize(id_t pointer, const type_t &t, node_t init, bool zero = true) {
        if (zero && (t.kind == T::array || t.kind == T::structure)) {
            if (auto literal = initializer_constant(t, init)) {
                store(pointer, constant(t, std::move(literal->value)));
                return;
            }
        }
        const auto parts = children(init);
        if (t.kind == T::array && init->data().kind == K::string_literal) {
            auto bytes = string_bytes(init->data().text);
            if (bytes.size() > *t.extent) bytes.resize(*t.extent);
            store(pointer, constant(t, std::move(bytes)));
            return;
        }
        if (init->data().kind != K::initializer_list) { store(pointer, expression(init)); return; }
        if (t.kind != T::array && t.kind != T::structure) {
            initialize(pointer, t, parts.at(0));
            return;
        }
        if (zero) {
            store(pointer, constant(t, std::monostate{}));
        }
        for (std::size_t i = 0; i < parts.size(); ++i) {
            const auto child_pointer = t.kind == T::array ? array_element(pointer, integer(static_cast<std::int64_t>(i))) :
                                                          field_address(pointer, i);
            const auto child_type = *type(child_pointer).element;
            initialize(child_pointer, child_type, parts[i], false);
        }
    }
    void declaration(node_t node) {
        const auto parts = children(node);
        if (parts.size() == 1) return;
        for (auto entry : children(parts[1])) {
            const bool initialized = entry->data().kind == K::initializer;
            const auto d = initialized ? children(entry).at(0) : entry;
            const auto sid = sem_.declared_symbols.at(declared_name(d)->data().id);
            const auto id = symbol_object(sid);
            if (initialized) initialize(address(id), type(id), children(entry).at(1));
        }
    }
    void statement(node_t node) {
        range_ = node->data().range;
        auto parts = children(node);
        switch (node->data().kind) {
        case K::declaration: declaration(node); return;
        case K::block: for (auto item : parts) statement(item); return;
        case K::empty: return;
        case K::expression_statement: expression(parts[0]); return;
        case K::return_statement:
            if (parts[0]->data().kind == K::empty) emit(O::return_value);
            else {
                auto result = convert(expression(parts[0]), *type(function().object).element);
                emit(O::return_value, A::object(result));
            }
            return;
        case K::break_statement: jump(loops_.back().first); return;
        case K::continue_statement: jump(loops_.back().second); return;
        case K::if_statement: {
            auto condition = convert(expression(parts[0]), primitive(T::boolean));
            const auto yes = label_++, no = label_++, done = label_++;
            emit(O::branch, A::object(condition), A::label(yes), A::label(no));
            label(yes); statement(parts[1]); jump(done);
            label(no); if (parts.size() == 3) statement(parts[2]); jump(done);
            label(done);
            return;
        }
        case K::while_statement: case K::for_statement: {
            const bool is_for = node->data().kind == K::for_statement;
            const auto test = label_++, body = label_++, step = label_++, done = label_++;
            if (is_for && parts[0]->data().kind != K::empty) {
                if (parts[0]->data().kind == K::declaration) declaration(parts[0]);
                else expression(parts[0]);
            }
            jump(test); label(test);
            const auto condition = parts[is_for ? 1 : 0];
            if (condition->data().kind == K::empty) jump(body);
            else {
                auto flag = convert(expression(condition), primitive(T::boolean));
                emit(O::branch, A::object(flag), A::label(body), A::label(done));
            }
            label(body);
            loops_.emplace_back(done, is_for ? step : test);
            statement(parts[is_for ? 3 : 1]);
            loops_.pop_back();
            jump(is_for ? step : test);
            if (is_for) {
                label(step);
                if (parts[2]->data().kind != K::empty) expression(parts[2]);
                jump(test);
            }
            label(done);
            return;
        }
        default: throw std::logic_error("Unsupported statement during HIR lowering");
        }
    }
    void finish() {
        const auto &code = function().operations;
        if (!code.empty() && is_terminator(code.back().opcode) && code.back().opcode != O::call && code.back().opcode != O::call_indirect)
            return;
        const auto &object = unit_.objects.at(function().object);
        if (object.type.element->kind == T::void_type) emit(O::return_value);
        else if (object.name == "main" && object.type.element->kind == T::integer) {
            auto zero = integer(0, T::integer);
            emit(O::return_value, A::object(zero));
        } else emit(O::unreachable); // reaching the end of a non-void function has no defined value
    }

    void reuse_object_addresses(function_t &f) {
        // Reuse repeated &object computations within a straight-line block.
        std::unordered_map<id_t, id_t> available, replaced;
        std::vector<operation_t> code;
        for (auto op : f.operations) {
            if (op.opcode == O::label) available.clear();
            for (auto &operand : op.operands)
                if (operand.kind == operand_kind_t::object)
                    if (const auto it = replaced.find(operand.id); it != replaced.end()) operand.id = it->second;
            for (auto &argument : op.arguments)
                if (const auto it = replaced.find(argument); it != replaced.end()) argument = it->second;
            if (op.opcode == O::address) {
                const auto [it, inserted] = available.emplace(op.operands[1].id, op.operands[0].id);
                if (!inserted) { replaced.emplace(op.operands[0].id, it->second); continue; }
            }
            if (is_terminator(op.opcode)) available.clear();
            code.push_back(std::move(op));
        }
        std::vector<bool> used(unit_.objects.size());
        for (const auto &op : code) {
            for (std::size_t i = 0; i < op.operands.size(); ++i)
                if (op.operands[i].kind == operand_kind_t::object && !(op.opcode == O::address && i == 0))
                    used[op.operands[i].id] = true;
            for (auto argument : op.arguments) used[argument] = true;
        }
        std::erase_if(code, [&](const auto &op) { return op.opcode == O::address && !used[op.operands[0].id]; });
        f.operations = std::move(code);
    }

  public:
    explicit lowerer_t(const frontend::semantic_result_t &sem) : sem_(sem) { unit_.structures = sem.structures; }
    translation_unit_t run(const frontend::ast_t &ast) {
        if (!sem_.ok()) throw std::invalid_argument("Cannot lower a program with semantic errors");
        if (!ast.check()) throw std::invalid_argument("Cannot lower an invalid AST");
        std::unordered_map<std::string, id_t> functions_by_name;
        for (const auto &symbol : sem_.symbols) {
            if (symbol.kind == frontend::symbol_kind_t::function) {
                const auto found = functions_by_name.find(symbol.name);
                if (found != functions_by_name.end() && type(found->second) == symbol.type) {
                    symbols_[symbol.id] = found->second;
                } else {
                    const auto id = object(symbol.name, symbol.type, storage_t::function);
                    symbols_[symbol.id] = id;
                    functions_by_name.emplace(symbol.name, id);
                }
            } else if (symbol.scope == 0) {
                const auto id = object(symbol.name, symbol.type, storage_t::global);
                symbols_[symbol.id] = id;
                unit_.objects[id].initializer = unit_.constants.size();
                unit_.constants.push_back({symbol.type, std::monostate{}});
            }
        }
        // Generate global initialization separately; function bodies cannot interleave it.
        for (auto node : children(ast.tree().root())) {
            if (node->data().kind != K::declaration) continue;
            const auto d = children(node);
            if (d.size() < 2) continue;
            for (auto entry : children(d[1])) {
                if (entry->data().kind != K::initializer) continue;
                if (!unit_.initializer) {
                    const auto init = object("$init", type_t::function_returning(primitive(T::void_type), {}), storage_t::function);
                    unit_.initializer = unit_.functions.size();
                    unit_.functions.push_back({init, {}, {}, {}, {}});
                }
                current_ = *unit_.initializer;
                range_ = entry->data().range;
                const auto p = children(entry);
                const auto id = symbol_object(sem_.declared_symbols.at(declared_name(p[0])->data().id));
                initialize(address(id), type(id), p[1]);
            }
        }
        if (unit_.initializer) { current_ = *unit_.initializer; finish(); }
        for (auto node : children(ast.tree().root())) {
            if (node->data().kind != K::function) continue;
            auto parts = children(node);
            const auto id = symbol_object(sem_.declared_symbols.at(declared_name(parts[1])->data().id));
            current_ = unit_.functions.size();
            unit_.functions.push_back({id, {}, {}, {}, {}});
            if (auto it = sem_.function_parameters.find(node->data().id); it != sem_.function_parameters.end())
                for (auto sid : it->second) function().parameters.push_back(symbol_object(sid));
            for (auto item : children(parts[2])) statement(item);
            finish();
        }
        for (auto &f : unit_.functions) { reuse_object_addresses(f); build_cfg(f); }
        return std::move(unit_);
    }
};
}
translation_unit_t lower(const frontend::ast_t &ast, const frontend::semantic_result_t &semantics) {
    return lowerer_t(semantics).run(ast);
}
} // namespace hir
