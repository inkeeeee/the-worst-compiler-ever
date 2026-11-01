#include "hir/hir.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace hir {
using O = opcode_t;
bool is_terminator(O op) {
    return op == O::jump || op == O::branch || op == O::return_value || op == O::unreachable ||
           op == O::call || op == O::call_indirect;
}
std::string_view opcode_name(O op) {
    switch (op) {
#define NAME(x) case O::x: return #x
        NAME(label); NAME(constant); NAME(convert); NAME(address); NAME(load); NAME(store);
        NAME(element_address); NAME(field_address); NAME(pointer_offset); NAME(pointer_difference);
        NAME(add); NAME(subtract); NAME(multiply); NAME(divide); NAME(remainder); NAME(shift_left); NAME(shift_right);
        NAME(bit_and); NAME(bit_or); NAME(bit_xor); NAME(negate); NAME(bit_not); NAME(logical_not);
        NAME(equal); NAME(not_equal); NAME(less); NAME(less_equal); NAME(greater); NAME(greater_equal);
        NAME(jump); NAME(branch); NAME(call); NAME(call_indirect); NAME(return_value); NAME(unreachable);
#undef NAME
    }
    return "<invalid opcode>";
}

void build_cfg(function_t &function) {
    id_t next = 0;
    for (const auto &op : function.operations)
        for (auto operand : op.operands)
            if (operand.kind == operand_kind_t::label) {
                if (operand.id == std::numeric_limits<id_t>::max()) throw std::invalid_argument("label ID overflow");
                next = std::max(next, operand.id + 1);
            }
    auto fresh_label = [&] {
        if (next == std::numeric_limits<id_t>::max()) throw std::invalid_argument("label ID overflow");
        return operand_t::label(next++);
    };
    std::vector<operation_t> code;
    for (const auto &op : function.operations) {
        if (op.opcode == O::label) {
            if (!code.empty() && !is_terminator(code.back().opcode))
                code.push_back({O::jump, {op.operands[0]}, {}, op.range});
        } else if (code.empty() || is_terminator(code.back().opcode)) {
            code.push_back({O::label, {fresh_label()}, {}, op.range});
        }
        code.push_back(op);
    }
    if (code.empty() || !is_terminator(code.back().opcode) || code.back().opcode == O::call ||
        code.back().opcode == O::call_indirect)
        throw std::invalid_argument("function must end with jump, branch, return or unreachable");
    cfg_t cfg;
    std::unordered_map<id_t, cfg_t::node_iterator> labels;
    std::vector<cfg_t::node_iterator> blocks;
    for (std::size_t begin = 0; begin < code.size();) {
        if (code[begin].opcode != O::label || code[begin].operands[0].kind != operand_kind_t::label)
            throw std::invalid_argument("basic block must begin with a label");
        auto end = begin + 1;
        while (end < code.size() && code[end].opcode != O::label) ++end;
        auto block = cfg.new_node(basic_block_t{code[begin].operands[0].id, begin, end});
        if (!labels.emplace(block->data().label, block).second) throw std::invalid_argument("duplicate label");
        blocks.push_back(block);
        begin = end;
    }
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const auto block = blocks[i];
        const auto &last = code[block->data().end - 1];
        const auto connect = [&](operand_t label, edge_kind_t kind) {
            if (label.kind != operand_kind_t::label || !labels.contains(label.id))
                throw std::invalid_argument("jump to an undefined label");
            cfg.new_edge(kind, block, labels.at(label.id));
        };
        if (last.opcode == O::jump) connect(last.operands[0], edge_kind_t::jump);
        else if (last.opcode == O::branch) {
            connect(last.operands[1], edge_kind_t::true_branch);
            connect(last.operands[2], edge_kind_t::false_branch);
        } else if (last.opcode == O::call || last.opcode == O::call_indirect) {
            if (i + 1 == blocks.size()) throw std::invalid_argument("call has no continuation");
            cfg.new_edge(edge_kind_t::continuation, block, blocks[i + 1]);
        } else if (!is_terminator(last.opcode)) throw std::invalid_argument("unterminated basic block");
    }
    function.operations = std::move(code);
    function.cfg = std::move(cfg);
}

namespace {
using T = frontend::type_kind_t;

std::string quoted_bytes(std::string_view bytes, char quote = '"') {
    std::ostringstream out;
    out << quote;
    for (unsigned char byte : bytes) {
        if (byte == static_cast<unsigned char>(quote) || byte == '\\') out << '\\' << byte;
        else switch (byte) {
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (byte >= 32 && byte < 127) out << byte;
            // Three octal digits keep an escape unambiguous before another digit.
            else out << '\\' << std::oct << std::setw(3) << std::setfill('0') << unsigned(byte) << std::dec;
        }
    }
    out << quote;
    return out.str();
}

// One formatter is shared by the text dump and CFG, so neither loses operand data.
class formatter_t {
    const translation_unit_t &unit_;
  public:
    explicit formatter_t(const translation_unit_t &unit) : unit_(unit) {}
    std::string type(const type_t &t) const {
        switch (t.kind) {
        case T::pointer: return "ptr<" + type(*t.element) + ">";
        case T::array: return "array[" + (t.extent ? std::to_string(*t.extent) : "?") + "]<" + type(*t.element) + ">";
        case T::structure: return t.record_name + "@" + std::to_string(*t.record);
        case T::function: {
            std::string result = "fn(";
            for (std::size_t i = 0; i < t.parameters.size(); ++i)
                result += (i ? ", " : "") + type(t.parameters[i]);
            return result + ") -> " + type(*t.element);
        }
        default: return t.str();
        }
    }
    std::string object(id_t id) const {
        const auto &o = unit_.objects.at(id);
        if (o.storage == storage_t::temporary) return "%" + std::to_string(id);
        return (o.storage == storage_t::function ? "@" : "%") + o.name + "." + std::to_string(id);
    }
    std::string typed_object(id_t id) const { return object(id) + " : " + type(unit_.objects.at(id).type); }
    std::string zero_value(const type_t &t) const {
        switch (t.kind) {
        case T::structure: {
            std::string result = "{";
            const auto &fields = unit_.structures.at(*t.record).fields;
            for (std::size_t i = 0; i < fields.size(); ++i)
                result += (i ? ", " : "") + fields[i].name + " = " + zero_value(fields[i].type);
            return result + "}";
        }
        case T::array: {
            const auto element = zero_value(*t.element);
            const auto count = *t.extent;
            // Exact repetition avoids dumping millions of identical array elements.
            if (count > 8 || (count && element.size() > 100 / count))
                return "repeat(" + element + ", " + std::to_string(count) + ")";
            std::string result = "[";
            for (std::size_t i = 0; i < count; ++i) result += (i ? ", " : "") + element;
            return result + "]";
        }
        case T::pointer: return "null";
        case T::boolean: return "false";
        case T::character: return "'\\0'";
        default: return "0";
        }
    }
    std::string constant_value(const constant_t &c) const {
        if (std::holds_alternative<std::monostate>(c.value)) return zero_value(c.type);
        if (auto members = std::get_if<aggregate_constant_t>(&c.value)) {
            if (c.type.kind == T::structure) {
                std::string result = "{";
                const auto &fields = unit_.structures.at(*c.type.record).fields;
                for (std::size_t i = 0; i < fields.size(); ++i)
                    result += (i ? ", " : "") + fields[i].name + " = " +
                              (i < members->size() ? constant_value((*members)[i]) : zero_value(fields[i].type));
                return result + "}";
            }
            const auto count = *c.type.extent;
            const auto shown = count <= 8 ? count : members->size();
            std::string result = "[";
            for (std::size_t i = 0; i < shown; ++i)
                result += (i ? ", " : "") + (i < members->size() ? constant_value((*members)[i]) : zero_value(*c.type.element));
            result += "]";
            if (shown < count)
                return "concat(" + result + ", repeat(" + zero_value(*c.type.element) + ", " + std::to_string(count - shown) + "))";
            return result;
        }
        if (auto bytes = std::get_if<std::string>(&c.value)) {
            const auto padding = *c.type.extent - bytes->size();
            if (padding > 8)
                return "concat(" + quoted_bytes(*bytes) + ", repeat('\\0', " + std::to_string(padding) + "))";
            return quoted_bytes(*bytes + std::string(padding, '\0'));
        }
        const auto number = std::get_if<std::int64_t>(&c.value);
        const auto bits = number ? static_cast<std::uint64_t>(*number) : std::get<std::uint64_t>(c.value);
        if (c.type.kind == T::boolean) return bits ? "true" : "false";
        if (c.type.kind == T::pointer && bits == 0) return "null";
        if (c.type.kind == T::character && bits <= 255) {
            if (bits == 0) return "'\\0'";
            return quoted_bytes(std::string(1, static_cast<char>(bits)), '\'');
        }
        return number ? std::to_string(*number) : std::to_string(bits);
    }
    std::string constant(id_t id) const { return constant_value(unit_.constants.at(id)); }
    std::string constants() const {
        std::string result = "constants {\n";
        for (std::size_t i = 0; i < unit_.constants.size(); ++i)
            result += "  constant #" + std::to_string(i) + " : " + type(unit_.constants[i].type) + " = " + constant(i) + '\n';
        return result + "}\n";
    }
    std::string operation(const operation_t &op) const {
        const auto a = op.operands[0].id, b = op.operands[1].id, c = op.operands[2].id;
        const auto label = [](id_t id) { return "L" + std::to_string(id); };
        const auto name = std::string(opcode_name(op.opcode));
        switch (op.opcode) {
        case O::label: return label(a) + ":";
        case O::jump: return "jump " + label(a);
        case O::branch: return "branch " + object(a) + " ? " + label(b) + " : " + label(c);
        case O::return_value: return "return_value" + (op.operands[0].kind == operand_kind_t::none ? "" : " " + typed_object(a));
        case O::unreachable: return "unreachable";
        case O::store: return "store " + object(a) + " <- " + typed_object(b);
        case O::call: case O::call_indirect: {
            std::string result;
            if (op.operands[1].kind != operand_kind_t::none) result = typed_object(b) + " = ";
            result += name + " " + object(a) + "(";
            for (std::size_t i = 0; i < op.arguments.size(); ++i)
                result += (i ? ", " : "") + object(op.arguments[i]);
            return result + ")";
        }
        case O::constant: return typed_object(a) + " = constant " + constant(b) + " (#" + std::to_string(b) + ")";
        case O::field_address: {
            const auto &record = unit_.structures.at(*unit_.objects.at(b).type.element->record);
            return typed_object(a) + " = field_address " + object(b) + ", ." + record.fields.at(c).name;
        }
        default: {
            auto result = typed_object(a) + " = " + name;
            for (std::size_t i = 1; i < op.operands.size(); ++i)
                if (op.operands[i].kind != operand_kind_t::none) result += (i == 1 ? " " : ", ") + object(op.operands[i].id);
            return result;
        }
        }
    }
    std::string structures() const {
        std::string result;
        for (const auto &record : unit_.structures) {
            result += "struct " + record.name + "@" + std::to_string(record.id);
            if (!record.defined) { result += ";\n"; continue; }
            result += " {\n";
            for (const auto &field : record.fields) result += "  " + field.name + " : " + type(field.type) + "\n";
            result += "}\n";
        }
        return result;
    }
    std::string globals() const {
        std::unordered_set<id_t> defined;
        for (const auto &f : unit_.functions) defined.insert(f.object);
        std::string result;
        for (id_t id = 0; id < unit_.objects.size(); ++id) {
            const auto &o = unit_.objects[id];
            if (o.owner) continue;
            if (o.storage == storage_t::function) {
                if (!defined.contains(id)) result += "extern " + typed_object(id) + "\n";
                continue;
            }
            result += (o.storage == storage_t::string_literal ? "string " : "global ") + typed_object(id);
            if (o.initializer) result += " = " + constant(*o.initializer) + " (#" + std::to_string(*o.initializer) + ")";
            result += '\n';
        }
        if (unit_.initializer) result += "initializer " + object(unit_.functions.at(*unit_.initializer).object) + " before main\n";
        return result;
    }
    std::string signature(const function_t &f) const {
        std::string result = object(f.object) + "(";
        for (std::size_t i = 0; i < f.parameters.size(); ++i)
            result += (i ? ", " : "") + typed_object(f.parameters[i]);
        return result + ") -> " + type(*unit_.objects.at(f.object).type.element);
    }
    std::string objects(const function_t &f) const {
        std::string result;
        for (auto id : f.locals)
            if (unit_.objects.at(id).storage == storage_t::local) result += typed_object(id) + "\n";
        return result;
    }
};

std::string dot_quote(std::string_view text, bool left_align = false) {
    std::string result = "\"";
    for (char c : text) {
        if (c == '\\' || c == '"') { result += '\\'; result += c; }
        else if (c == '\n') result += left_align ? "\\l" : "\\n";
        else if (c == '\r') result += "\\r";
        else result += c;
    }
    return result + '"';
}

std::string html_quote(std::string_view text) {
    std::string result;
    for (char c : text) {
        switch (c) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\n': result += "<BR ALIGN=\"LEFT\"/>"; break;
        default: result += c;
        }
    }
    return result;
}

std::string table_label(std::string_view title, const std::vector<std::string> &columns,
                        const std::vector<std::vector<std::string>> &rows) {
    std::string result = "<<TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"5\">"
                         "<TR><TD COLSPAN=\"" + std::to_string(columns.size()) + "\"><B>" + html_quote(title) + "</B></TD></TR><TR>";
    for (const auto &column : columns) result += "<TD ALIGN=\"LEFT\"><B>" + html_quote(column) + "</B></TD>";
    result += "</TR>";
    for (const auto &row : rows) {
        result += "<TR>";
        for (const auto &cell : row) result += "<TD ALIGN=\"LEFT\">" + html_quote(cell) + "</TD>";
        result += "</TR>";
    }
    return result + "</TABLE>>";
}

std::string_view edge_name(edge_kind_t kind) {
    switch (kind) {
    case edge_kind_t::jump: return "jump";
    case edge_kind_t::true_branch: return "true";
    case edge_kind_t::false_branch: return "false";
    case edge_kind_t::continuation: return "return from call";
    }
    return "?";
}
}
void write(const translation_unit_t &unit, std::ostream &out) {
    const formatter_t format(unit);
    out << format.structures() << format.constants();
    out << format.globals();
    for (const auto &f : unit.functions) {
        out << "\nfunction " << format.signature(f) << " {\n";
        const auto objects = format.objects(f);
        if (!objects.empty()) {
            out << "  objects {\n";
            std::istringstream lines(objects);
            for (std::string line; std::getline(lines, line);) out << "    " << line << '\n';
            out << "  }\n";
        }
        for (const auto &op : f.operations) out << "  " << format.operation(op) << '\n';
        out << "}\n";
    }
    if (!out) throw std::ios_base::failure("Cannot write HIR");
}
void write_cfg_dot(const translation_unit_t &unit, std::ostream &out) {
    const formatter_t format(unit);
    out << "digraph G {\n"
           "  graph [rankdir=TB, fontname=\"Courier\", fontsize=12, nodesep=0.35, ranksep=0.5];\n"
           "  node [shape=box, fontname=\"Courier\", fontsize=10, margin=\"0.16,0.12\"];\n"
           "  edge [fontname=\"Courier\", fontsize=9, arrowsize=0.7];\n";
    const auto declarations = format.structures() + format.globals();
    if (!declarations.empty())
        out << "  declarations [shape=note, label=" << dot_quote(declarations, true) << "];\n";
    if (!unit.constants.empty()) {
        std::vector<std::vector<std::string>> rows;
        for (std::size_t i = 0; i < unit.constants.size(); ++i)
            rows.push_back({"#" + std::to_string(i), format.type(unit.constants[i].type), format.constant(i)});
        out << "  constants [shape=plain, label=" << table_label("Constants", {"ID", "Type", "Value"}, rows) << "];\n";
    }
    for (std::size_t fi = 0; fi < unit.functions.size(); ++fi) {
        const auto &f = unit.functions[fi];
        if (!f.cfg.check()) throw std::invalid_argument("Invalid CFG");
        const auto node_name = [&](id_t label) { return "f" + std::to_string(fi) + "_L" + std::to_string(label); };
        out << "  subgraph cluster_f" << fi << " {\n    label=" << dot_quote(format.signature(f)) << ";\n    labeljust=l;\n";
        std::vector<std::vector<std::string>> objects;
        for (auto id : f.locals)
            if (unit.objects.at(id).storage == storage_t::local)
                objects.push_back({format.object(id), format.type(unit.objects.at(id).type)});
        if (!objects.empty())
            out << "    f" << fi << "_objects [shape=plain, label=" << table_label("Objects", {"ID", "Type"}, objects) << "];\n";
        bool entry = true;
        for (const auto &node : f.cfg) {
            std::string text = "L" + std::to_string(node.data().label) + (entry ? " (entry)\n" : "\n");
            for (auto i = node.data().begin + 1; i < node.data().end; ++i) text += format.operation(f.operations.at(i)) + '\n';
            out << "    " << node_name(node.data().label) << " [label=" << dot_quote(text, true) << "];\n";
            entry = false;
        }
        for (const auto &node : f.cfg)
            for (auto it = node.out_begin(); it != node.out_end(); ++it) {
                const auto &edge = std::as_const(**it);
                out << "    " << node_name(node.data().label) << " -> " << node_name(edge.next_node()->data().label)
                    << " [label=" << dot_quote(edge_name(edge.data())) << "];\n";
            }
        out << "  }\n";
    }
    out << "}\n";
    if (!out) throw std::ios_base::failure("Cannot write CFG");
}
} // namespace hir
