#include "hir/hir.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace hir {
namespace {
using T = frontend::type_kind_t;
using O = opcode_t;
using A = operand_kind_t;

class verifier_t {
    const translation_unit_t &u_;
    std::vector<diagnostic_t> errors_;
    std::optional<id_t> fi_, oi_;
    void error(std::string message) { errors_.push_back({fi_, oi_, std::move(message)}); }
    bool valid_type(const type_t &t, bool complete = false, unsigned depth = 0) const {
        if (depth > 128) return false;
        switch (t.kind) {
        case T::error: return false;
        case T::structure:
            return t.record && *t.record < u_.structures.size() && (!complete || u_.structures[*t.record].defined);
        case T::array:
            return t.element && (!complete || t.extent) && (!t.extent || *t.extent > 0) && t.element->kind != T::void_type &&
                   t.element->kind != T::function && valid_type(*t.element, true, depth + 1);
        case T::pointer: return t.element && valid_type(*t.element, false, depth + 1);
        case T::function:
            if (!t.element || t.element->kind == T::function || t.element->kind == T::array ||
                !valid_type(*t.element, false, depth + 1)) return false;
            for (const auto &p : t.parameters)
                if (p.kind == T::void_type || p.kind == T::function || p.kind == T::array || !valid_type(p, false, depth + 1))
                    return false;
            return true;
        default: return t.kind == T::void_type || t.is_integer();
        }
    }
    bool object_type(const type_t &t) const {
        return t.kind != T::void_type && t.kind != T::function && valid_type(t, true);
    }
    bool scalar_constant(const constant_t &c) const {
        const auto signed_value = std::get_if<std::int64_t>(&c.value);
        const auto unsigned_value = std::get_if<std::uint64_t>(&c.value);
        if (!signed_value && !unsigned_value) return true;
        if (c.type.kind == T::pointer) return signed_value ? *signed_value == 0 : *unsigned_value == 0;
        if (!c.type.is_integer()) return false;
        std::int64_t minimum = std::numeric_limits<std::int64_t>::min();
        std::uint64_t maximum = std::numeric_limits<std::int64_t>::max();
        switch (c.type.kind) {
        case T::boolean: minimum=0; maximum=1; break;
        // Character literals and strings may carry an unsigned byte spelling.
        case T::character: minimum=-128; maximum=255; break;
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
        if (unsigned_value) return *unsigned_value <= maximum;
        return *signed_value >= minimum && (*signed_value < 0 || static_cast<std::uint64_t>(*signed_value) <= maximum);
    }
    void constant(const constant_t &c, unsigned depth = 0) {
        if (depth > 128) { error("constant nesting is too deep"); return; }
        if (!object_type(c.type)) { error("invalid constant type"); return; }
        if (auto text = std::get_if<std::string>(&c.value)) {
            if (c.type.kind != T::array || c.type.element->kind != T::character || text->size() > *c.type.extent)
                error("string constant type/size mismatch");
        } else if (auto members = std::get_if<aggregate_constant_t>(&c.value)) {
            if (c.type.kind != T::array && c.type.kind != T::structure) {
                error("aggregate constant requires an array or struct type"); return;
            }
            const auto count = c.type.kind == T::array ? *c.type.extent : u_.structures.at(*c.type.record).fields.size();
            if (members->size() > count) { error("aggregate constant has too many members"); return; }
            for (std::size_t i = 0; i < members->size(); ++i) {
                const auto &expected = c.type.kind == T::array ? *c.type.element : u_.structures.at(*c.type.record).fields[i].type;
                if ((*members)[i].type != expected) error("aggregate constant member type mismatch");
                constant((*members)[i], depth + 1);
            }
        } else if (!scalar_constant(c)) error("constant value is out of range for its type");
    }
    const type_t &type(id_t id) const { return u_.objects[id].type; }
    static bool local(storage_t s) {
        return s == storage_t::local || s == storage_t::parameter || s == storage_t::temporary;
    }
    bool reference(id_t id) {
        if (id >= u_.objects.size()) { error("object index is out of range"); return false; }
        if (local(u_.objects[id].storage) && u_.objects[id].owner != fi_) {
            error("reference to an object owned by another function"); return false;
        }
        return true;
    }
    bool writable(id_t id) {
        if (u_.objects[id].storage == storage_t::function || u_.objects[id].storage == storage_t::string_literal) {
            error("operation result is not a writable object"); return false;
        }
        return true;
    }
    void records() {
        std::vector<unsigned> colors(u_.structures.size());
        std::function<bool(const type_t &)> visit_type;
        std::function<bool(id_t)> visit = [&](id_t id) {
            if (colors[id] == 1) return false;
            if (colors[id] == 2) return true;
            colors[id] = 1;
            for (const auto &field : u_.structures[id].fields) if (!visit_type(field.type)) return false;
            colors[id] = 2;
            return true;
        };
        visit_type = [&](const type_t &t) {
            if (t.kind == T::array && t.element) return visit_type(*t.element);
            if (t.kind == T::structure && t.record && *t.record < colors.size()) return visit(*t.record);
            return true;
        };
        for (std::size_t i = 0; i < u_.structures.size(); ++i) {
            const auto &record = u_.structures[i];
            if (record.id != i) error("struct index does not match its ID");
            if (record.defined && record.fields.empty()) error("empty struct definition");
            if (!record.defined && !record.fields.empty()) error("incomplete struct has fields");
            std::unordered_set<std::string> names;
            for (const auto &field : record.fields) {
                if (!names.insert(field.name).second) error("duplicate struct field");
                if (!object_type(field.type)) error("invalid or incomplete struct field type");
            }
            if (!visit(i)) { error("recursive struct layout by value"); break; }
        }
    }
    bool instruction(const operation_t &op, std::vector<id_t> &reads, std::vector<id_t> &writes) {
        std::vector<A> shape;
        bool result = false;
        switch (op.opcode) {
        case O::label: case O::jump: shape = {A::label}; break;
        case O::branch: shape = {A::object, A::label, A::label}; break;
        case O::return_value:
            if (op.operands[0].kind != A::none) shape = {A::object};
            break;
        case O::unreachable: break;
        case O::call: case O::call_indirect:
            shape = {A::object};
            if (op.operands[1].kind != A::none) shape.push_back(A::object);
            break;
        case O::constant: shape = {A::object, A::constant}; result = true; break;
        case O::field_address: shape = {A::object, A::object, A::field}; result = true; break;
        case O::store: shape = {A::object, A::object}; break;
        case O::convert: case O::address: case O::load:
        case O::negate: case O::bit_not: case O::logical_not:
            shape = {A::object, A::object}; result = true; break;
        case O::element_address: case O::pointer_offset: case O::pointer_difference:
        case O::add: case O::subtract: case O::multiply: case O::divide: case O::remainder:
        case O::shift_left: case O::shift_right: case O::bit_and: case O::bit_or: case O::bit_xor:
        case O::equal: case O::not_equal: case O::less: case O::less_equal: case O::greater: case O::greater_equal:
            shape = {A::object, A::object, A::object}; result = true; break;
        default: error("unknown operation"); return false;
        }
        bool valid = true;
        for (std::size_t i = 0; i < op.operands.size(); ++i) {
            auto a = op.operands[i];
            if (a.kind != (i < shape.size() ? shape[i] : A::none)) { error("wrong operand kind or arity"); valid = false; }
            if (a.kind == A::object && !reference(a.id)) valid = false;
            if (a.kind == A::constant && a.id >= u_.constants.size()) { error("constant index is out of range"); valid = false; }
        }
        if (op.opcode != O::call && op.opcode != O::call_indirect && !op.arguments.empty()) {
            error("only calls accept variable arguments"); valid = false;
        }
        for (auto id : op.arguments) if (!reference(id)) valid = false;
        if (!valid) return false;
        for (std::size_t i = 0; i < shape.size(); ++i) {
            if (op.operands[i].kind != A::object) continue;
            const auto id = op.operands[i].id;
            if ((result && i == 0) || ((op.opcode == O::call || op.opcode == O::call_indirect) && i == 1)) {
                writable(id); writes.push_back(id);
            } else if (!(op.opcode == O::address && i == 1)) reads.push_back(id);
        }
        reads.insert(reads.end(), op.arguments.begin(), op.arguments.end());
        const auto a = op.operands[0].id, b = op.operands[1].id, c = op.operands[2].id;
        auto require = [&](bool condition, std::string message) { if (!condition) error(std::move(message)); };
        auto pointer = [&](id_t id) { return type(id).kind == T::pointer && type(id).element; };
        const auto &signature = type(u_.functions[*fi_].object);
        switch (op.opcode) {
        case O::label: case O::jump: case O::unreachable: break;
        case O::constant: require(type(a) == u_.constants[b].type, "constant/result type mismatch"); break;
        case O::convert:
            require(type(a).is_scalar() && type(b).is_scalar(), "conversion requires scalar types"); break;
        case O::address:
            require(pointer(a) && *type(a).element == type(b), "address/result type mismatch"); break;
        case O::load:
            require(pointer(b) && object_type(*type(b).element) && type(a) == *type(b).element, "load type mismatch"); break;
        case O::store:
            require(pointer(a) && object_type(*type(a).element) && *type(a).element == type(b), "store type mismatch"); break;
        case O::element_address:
            require(pointer(b) && type(b).element->kind == T::array && pointer(a) &&
                    *type(a).element == *type(b).element->element && type(c).kind == T::long_integer,
                    "array address requires pointer-to-array and long index"); break;
        case O::field_address:
            if (!pointer(b) || type(b).element->kind != T::structure) { error("field address requires pointer-to-struct"); break; }
            {
                const auto &fields = u_.structures.at(*type(b).element->record).fields;
                require(c < fields.size(), "field index is out of range");
                if (c < fields.size()) require(pointer(a) && *type(a).element == fields[c].type, "field address result type mismatch");
            }
            break;
        case O::pointer_offset:
            require(pointer(b) && object_type(*type(b).element) && type(a) == type(b) && type(c).kind == T::long_integer,
                    "pointer offset type mismatch"); break;
        case O::pointer_difference:
            require(pointer(b) && object_type(*type(b).element) && type(b) == type(c) && type(a).kind == T::long_integer,
                    "pointer difference type mismatch"); break;
        case O::negate: case O::bit_not:
            require(type(a).is_integer() && type(a) == type(b), "unary integer operation type mismatch"); break;
        case O::logical_not:
            require(type(a).kind == T::boolean && type(b).kind == T::boolean, "logical not requires bool"); break;
        case O::shift_left: case O::shift_right:
            require(type(a).is_integer() && type(a) == type(b) && type(c).kind == T::long_integer, "shift type mismatch"); break;
        case O::equal: case O::not_equal: case O::less: case O::less_equal: case O::greater: case O::greater_equal:
            require(type(a).kind == T::boolean && type(b).is_scalar() && type(b) == type(c), "comparison type mismatch"); break;
        case O::add: case O::subtract: case O::multiply: case O::divide: case O::remainder:
        case O::bit_and: case O::bit_or: case O::bit_xor:
            require(type(a).is_integer() && type(a) == type(b) && type(a) == type(c), "arithmetic requires identical integer types"); break;
        case O::branch: require(type(a).kind == T::boolean, "branch requires bool"); break;
        case O::return_value:
            if (signature.element->kind == T::void_type) require(shape.empty(), "void function returns a value");
            else require(!shape.empty() && type(a) == *signature.element, "return type mismatch");
            break;
        case O::call: case O::call_indirect: {
            const auto *callee = &type(a);
            if (op.opcode == O::call_indirect) {
                if (!pointer(a)) { error("indirect call requires a function pointer"); break; }
                callee = type(a).element.get();
            } else require(u_.objects[a].storage == storage_t::function, "direct call requires a function object");
            if (callee->kind != T::function) { error("call target has no function type"); break; }
            require(callee->parameters.size() == op.arguments.size(), "call argument count mismatch");
            for (std::size_t i = 0; i < std::min(callee->parameters.size(), op.arguments.size()); ++i)
                require(callee->parameters[i] == type(op.arguments[i]), "call argument type mismatch");
            if (callee->element->kind == T::void_type) require(shape.size() == 1, "void call has a result");
            else require(shape.size() == 2 && type(b) == *callee->element, "call result type mismatch");
            break;
        }
        }
        return true;
    }

    bool cfg(const function_t &f) {
        if (!f.cfg.check()) { error("invalid graph topology"); return false; }
        auto expected = f;
        try { build_cfg(expected); }
        catch (const std::exception &ex) { error(ex.what()); return false; }
        bool normalized = expected.operations.size() == f.operations.size();
        if (normalized)
            for (std::size_t i = 0; i < f.operations.size(); ++i) {
                const auto &a = expected.operations[i], &b = f.operations[i];
                if (a.opcode != b.opcode || a.operands != b.operands || a.arguments != b.arguments) normalized = false;
            }
        if (!normalized) { error("IR has missing labels or explicit fallthrough transitions"); return false; }
        auto describe = [](const cfg_t &graph) {
            std::vector<std::tuple<id_t, id_t, id_t>> blocks;
            std::multiset<std::tuple<id_t, edge_kind_t, id_t>> edges;
            for (const auto &node : graph) {
                const auto &b = node.data();
                blocks.emplace_back(b.label, b.begin, b.end);
                for (auto it = node.out_begin(); it != node.out_end(); ++it) {
                    const auto &edge = std::as_const(**it);
                    edges.emplace(b.label, edge.data(), edge.next_node()->data().label);
                }
            }
            return std::pair{blocks, edges};
        };
        if (describe(expected.cfg) != describe(f.cfg)) { error("CFG does not match the operation stream"); return false; }
        return true;
    }
    void definitions(const function_t &f, const std::vector<std::vector<id_t>> &reads,
                     const std::vector<std::vector<id_t>> &writes) {
        std::vector<basic_block_t> blocks;
        std::unordered_map<const cfg_t::node_t *, id_t> indices;
        for (const auto &node : f.cfg) { indices[&node] = blocks.size(); blocks.push_back(node.data()); }
        if (blocks.empty()) return;
        std::vector<std::vector<id_t>> pred(blocks.size()), succ(blocks.size());
        for (const auto &node : f.cfg) {
            auto i = indices.at(&node);
            for (auto it = node.out_begin(); it != node.out_end(); ++it) {
                const auto j = indices.at(&*std::as_const(**it).next_node());
                succ[i].push_back(j); pred[j].push_back(i);
            }
        }
        std::vector<bool> reached(blocks.size());
        std::vector<id_t> todo{0};
        while (!todo.empty()) {
            auto i = todo.back(); todo.pop_back();
            if (reached[i]) continue;
            reached[i] = true;
            for (auto j : succ[i]) todo.push_back(j);
        }
        std::vector<bool> entry(u_.objects.size(), true);
        for (auto id : f.locals) if (u_.objects[id].storage != storage_t::parameter) entry[id] = false;
        const std::vector<bool> top(u_.objects.size(), true);
        std::vector<std::vector<bool>> in(blocks.size(), top), out(blocks.size(), top);
        bool changed;
        do {
            changed = false;
            for (id_t i = 0; i < blocks.size(); ++i) {
                if (!reached[i]) continue;
                auto state = i == 0 ? entry : top;
                for (auto p : pred[i]) if (reached[p])
                    for (id_t id = 0; id < state.size(); ++id) state[id] = state[id] && out[p][id];
                in[i] = state;
                for (auto op = blocks[i].begin; op < blocks[i].end; ++op)
                    for (auto id : writes[op]) state[id] = true;
                if (state != out[i]) { out[i] = std::move(state); changed = true; }
            }
        } while (changed);
        for (id_t i = 0; i < blocks.size(); ++i) {
            if (!reached[i]) continue;
            auto state = in[i];
            for (auto op = blocks[i].begin; op < blocks[i].end; ++op) {
                oi_ = op;
                for (auto id : reads[op]) if (!state[id]) error("object %" + std::to_string(id) + " is used before definition on some control-flow path");
                for (auto id : writes[op]) state[id] = true;
            }
        }
        oi_.reset();
    }
  public:
    explicit verifier_t(const translation_unit_t &unit) : u_(unit) {}
    std::vector<diagnostic_t> run() {
        records();
        for (const auto &c : u_.constants) constant(c);
        for (id_t i = 0; i < u_.objects.size(); ++i) {
            const auto &object = u_.objects[i];
            if (object.storage == storage_t::function ? (object.type.kind != T::function || !valid_type(object.type)) : !object_type(object.type))
                error("invalid object type for %" + std::to_string(i));
            if (local(object.storage)) {
                if (!object.owner || *object.owner >= u_.functions.size()) error("local object has no valid owner");
            } else if (object.owner) error("global object has a function owner");
            if (object.initializer) {
                if (local(object.storage) || object.storage == storage_t::function) error("invalid static initializer placement");
                if (*object.initializer >= u_.constants.size() || u_.constants[*object.initializer].type != object.type)
                    error("object initializer type/index mismatch");
            }
        }
        if (u_.initializer && *u_.initializer >= u_.functions.size()) error("invalid initializer function index");
        if (!errors_.empty()) return errors_; // type validation protects all following dereferences
        std::set<id_t> defined_functions;
        std::vector<unsigned> listed(u_.objects.size());
        for (id_t f = 0; f < u_.functions.size(); ++f) {
            fi_ = f;
            const auto &fn = u_.functions[f];
            if (fn.object >= u_.objects.size() || u_.objects[fn.object].storage != storage_t::function) {
                error("invalid function object"); continue;
            }
            if (!defined_functions.insert(fn.object).second) error("duplicate function definition");
            const auto &signature = type(fn.object);
            if (signature.element->kind != T::void_type && !object_type(*signature.element)) error("incomplete function return type");
            std::unordered_set<id_t> locals, params;
            bool safe = true;
            for (auto id : fn.locals) {
                if (!reference(id)) { safe = false; continue; }
                if (!local(u_.objects[id].storage) || !locals.insert(id).second) error("invalid/duplicate local object");
                ++listed[id];
            }
            if (fn.parameters.size() != signature.parameters.size()) error("function parameter count mismatch");
            for (id_t i = 0; i < fn.parameters.size(); ++i) {
                auto id = fn.parameters[i];
                if (!reference(id)) { safe = false; continue; }
                if (!locals.contains(id) || u_.objects[id].storage != storage_t::parameter || !params.insert(id).second)
                    error("invalid function parameter object");
                if (i < signature.parameters.size() && type(id) != signature.parameters[i]) error("function parameter type mismatch");
            }
            for (auto id : locals)
                if (u_.objects[id].storage == storage_t::parameter && !params.contains(id)) error("unlisted function parameter");
            if (u_.initializer == f && (!signature.parameters.empty() || signature.element->kind != T::void_type))
                error("initializer must have type function() -> void");
            std::vector<std::vector<id_t>> reads(fn.operations.size()), writes(fn.operations.size());
            for (id_t i = 0; i < fn.operations.size(); ++i) {
                oi_ = i;
                if (!instruction(fn.operations[i], reads[i], writes[i])) safe = false;
            }
            oi_.reset();
            if (safe && cfg(fn)) definitions(fn, reads, writes);
        }
        fi_.reset(); oi_.reset();
        for (id_t i = 0; i < u_.objects.size(); ++i)
            if (local(u_.objects[i].storage) && listed[i] != 1) error("local object missing from its owner's object list");
        return errors_;
    }
};
}
std::vector<diagnostic_t> verify(const translation_unit_t &unit) { return verifier_t(unit).run(); }
} // namespace hir
