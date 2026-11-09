#include "lir/lir.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace lir {
using O = opcode_t;
using A = operand_t;
bool is_terminator(O op) {
    return op == O::jump || op == O::branch || op == O::call || op == O::ret || op == O::trap;
}
std::string_view opcode_name(O op) {
    switch (op) {
#define NAME(x)                                                                                                        \
    case O::x:                                                                                                         \
        return #x
        NAME(label);
        NAME(mov);
        NAME(ld);
        NAME(st);
        NAME(add);
        NAME(sub);
        NAME(mul);
        NAME(div);
        NAME(rem);
        NAME(shl);
        NAME(shr);
        NAME(bit_and);
        NAME(bit_or);
        NAME(bit_xor);
        NAME(neg);
        NAME(bit_not);
        NAME(eq);
        NAME(ne);
        NAME(lt);
        NAME(le);
        NAME(gt);
        NAME(ge);
        NAME(jump);
        NAME(branch);
        NAME(call);
        NAME(ret);
        NAME(trap);
#undef NAME
    }
    return "<invalid>";
}
void build_cfg(function_t &f) {
    id_t next = 0;
    for (const auto &op : f.operations)
        for (auto a : op.operands)
            if (a.kind == operand_kind_t::label) {
                if (a.value == std::numeric_limits<id_t>::max()) throw std::invalid_argument("LIR: label overflow");
                next = std::max(next, static_cast<id_t>(a.value + 1));
            }
    auto fresh = [&] {
        if (next == std::numeric_limits<id_t>::max()) throw std::invalid_argument("LIR: label overflow");
        return A::label(next++);
    };
    std::vector<operation_t> code;
    for (const auto &op : f.operations) {
        if (op.opcode == O::label) {
            if (!code.empty() && !is_terminator(code.back().opcode))
                code.push_back({O::jump, scalar_t::u64, {op.operands[0]}});
        } else if (code.empty() || is_terminator(code.back().opcode))
            code.push_back({O::label, scalar_t::u64, {fresh()}});
        code.push_back(op);
    }
    if (code.empty() || !is_terminator(code.back().opcode) || code.back().opcode == O::call)
        throw std::invalid_argument("LIR: unterminated function");
    hir::cfg_t graph;
    std::unordered_map<id_t, hir::cfg_t::node_iterator> labels;
    std::vector<hir::cfg_t::node_iterator> blocks;
    for (std::size_t begin = 0; begin < code.size();) {
        if (code[begin].opcode != O::label || code[begin].operands[0].kind != operand_kind_t::label)
            throw std::invalid_argument("LIR: block must begin with label");
        auto end = begin + 1;
        while (end < code.size() && code[end].opcode != O::label) ++end;
        auto node = graph.new_node(hir::basic_block_t{static_cast<id_t>(code[begin].operands[0].value), begin, end});
        if (!labels.emplace(node->data().label, node).second) throw std::invalid_argument("LIR: duplicate label");
        blocks.push_back(node);
        begin = end;
    }
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const auto block = blocks[i];
        const auto &last = code[block->data().end - 1];
        const auto connect = [&](A label, hir::edge_kind_t kind) {
            if (label.kind != operand_kind_t::label || !labels.contains(label.value))
                throw std::invalid_argument("LIR: undefined label");
            graph.new_edge(kind, block, labels.at(label.value));
        };
        if (last.opcode == O::jump)
            connect(last.operands[0], hir::edge_kind_t::jump);
        else if (last.opcode == O::branch) {
            connect(last.operands[1], hir::edge_kind_t::true_branch);
            connect(last.operands[2], hir::edge_kind_t::false_branch);
        } else if (last.opcode == O::call) {
            if (i + 1 == blocks.size()) throw std::invalid_argument("LIR: call without continuation");
            graph.new_edge(hir::edge_kind_t::continuation, block, blocks[i + 1]);
        } else if (!is_terminator(last.opcode))
            throw std::invalid_argument("LIR: unterminated block");
    }
    f.operations = std::move(code);
    f.cfg = std::move(graph);
}
namespace {
std::string register_name(reg_t reg) {
    return reg == sp ? "sp" : reg == bp ? "bp" : "v" + std::to_string(reg - 2);
}
std::string type_name(scalar_t t) {
    return std::string(is_signed(t) ? "i" : "u") + std::to_string(width(t) * 8);
}
std::string offset_text(std::int64_t n) {
    return n == 0 ? "" : n < 0 ? std::to_string(n) : "+" + std::to_string(n);
}
std::string bits_text(std::uint64_t bits, scalar_t type) {
    if (is_signed(type)) {
        const unsigned shift = 64 - width(type) * 8;
        const auto signed_value = std::bit_cast<std::int64_t>(bits << shift) >> shift;
        return std::to_string(signed_value);
    }
    return std::to_string(bits);
}
std::string operand(const translation_unit_t &u, A a) {
    switch (a.kind) {
    case operand_kind_t::reg:
        return register_name(a.value);
    case operand_kind_t::immediate:
        return std::to_string(a.value);
    case operand_kind_t::symbol:
        return "@" + u.symbols.at(a.value).name;
    case operand_kind_t::label:
        return "L" + std::to_string(a.value);
    default:
        return "";
    }
}
std::string instruction(const translation_unit_t &u, const operation_t &op) {
    const auto a = operand(u, op.operands[0]), b = operand(u, op.operands[1]);
    if (op.opcode == O::label) return a + ":";
    std::string code(opcode_name(op.opcode));
    if (op.opcode != O::jump && op.opcode != O::branch && op.opcode != O::call && op.opcode != O::ret &&
        op.opcode != O::trap)
        code += "." + type_name(op.type);
    if (op.opcode == O::ld || op.opcode == O::st) {
        const auto displacement = offset_text(std::bit_cast<std::int64_t>(op.operands[2].value));
        return code + " " +
               (op.opcode == O::ld ? a + ", [" + b + displacement + "]" : "[" + a + displacement + "], " + b);
    }
    bool first = true;
    for (auto arg : op.operands) {
        if (arg.kind == operand_kind_t::none) break;
        code += first ? " " : ", ";
        first = false;
        code += arg.kind == operand_kind_t::immediate ? bits_text(arg.value, op.type) : operand(u, arg);
    }
    return code;
}
std::string map_entry(const location_t &where) {
    const auto name =
        where.temporary ? "%" + std::to_string(where.object) : "%" + where.name + "." + std::to_string(where.object);
    return name + " -> " +
           (where.kind == location_kind_t::reg ? register_name(where.reg) : "[bp" + offset_text(where.offset) + "]") +
           " (" + std::to_string(where.size) + " bytes)";
}
std::string data_description(const translation_unit_t &u, const data_t &data) {
    std::ostringstream out;
    out << '@' << u.symbols.at(data.symbol).name << " size=" << data.size << " align=" << data.alignment;
    out << (data.read_only ? " readonly" : " writable") << "; zero-filled";
    for (const auto &value : data.values)
        out << "\n  +" << value.offset << ": " << type_name(value.type) << ' ' << bits_text(value.bits, value.type);
    return out.str();
}
std::string dot_quote(std::string_view value) {
    std::string result = "\"";
    for (char c : value) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n')
            result += "\\l";
        else
            result += c;
    }
    return result + "\\l\"";
}
} // namespace
void write(const translation_unit_t &unit, std::ostream &output) {
    output << "target lp64 little-endian; register=8; stack-alignment=16\n";
    output << "data {\n";
    for (const auto &data : unit.data) output << "  " << data_description(unit, data) << '\n';
    output << "}\n";
    if (unit.initializer) output << "initializer @" << unit.symbols.at(*unit.initializer).name << '\n';
    for (id_t id = 0; id < unit.symbols.size(); ++id)
        if (unit.symbols[id].function &&
            std::none_of(unit.functions.begin(), unit.functions.end(), [&](const auto &f) { return f.symbol == id; }))
            output << "extern @" << unit.symbols[id].name << '\n';
    for (const auto &f : unit.functions) {
        output << "\nfunction @" << unit.symbols.at(f.symbol).name << " frame=" << f.frame_size << " {\n  storage {\n";
        for (const auto &where : f.locations)
            if (!where.temporary || where.kind == location_kind_t::stack) output << "    " << map_entry(where) << '\n';
        output << "  }\n";
        for (const auto &op : f.operations)
            output << (op.opcode == O::label ? "" : "  ") << instruction(unit, op) << '\n';
        output << "}\n";
    }
}
void write_cfg_dot(const translation_unit_t &unit, std::ostream &output) {
    output << "digraph LIR {\n  graph [fontname=\"Courier\", fontsize=11];\n"
              "  node [shape=box, fontname=\"Courier\", fontsize=10, margin=\"0.16,0.12\"];\n"
              "  edge [fontname=\"Courier\", fontsize=9];\n";
    if (!unit.data.empty()) {
        std::string text = "Data\n";
        for (const auto &data : unit.data) text += data_description(unit, data) + "\n";
        output << "  data [label=" << dot_quote(text) << "];\n";
    }
    for (id_t i = 0; i < unit.functions.size(); ++i) {
        const auto &f = unit.functions[i];
        const auto prefix = "f" + std::to_string(i) + "_";
        output << "  subgraph cluster_" << i << " {\n    label="
               << dot_quote("@" + unit.symbols.at(f.symbol).name + " frame=" + std::to_string(f.frame_size)) << ";\n";
        std::string storage = "Storage\n";
        for (const auto &where : f.locations)
            if (!where.temporary || where.kind == location_kind_t::stack) storage += map_entry(where) + '\n';
        output << "    " << prefix << "storage [label=" << dot_quote(storage) << "];\n";
        for (const auto &node : f.cfg) {
            const auto &block = node.data();
            std::string text;
            for (auto j = block.begin; j < block.end; ++j) text += instruction(unit, f.operations[j]) + '\n';
            output << "    " << prefix << 'L' << block.label << " [label=" << dot_quote(text) << "];\n";
            for (auto it = node.out_begin(); it != node.out_end(); ++it) {
                const auto &edge = std::as_const(**it);
                const auto label = edge.data() == hir::edge_kind_t::true_branch    ? "true"
                                   : edge.data() == hir::edge_kind_t::false_branch ? "false"
                                   : edge.data() == hir::edge_kind_t::continuation ? "return"
                                                                                   : "jump";
                output << "    " << prefix << 'L' << block.label << " -> " << prefix << 'L'
                       << edge.next_node()->data().label << " [label=\"" << label << "\"];\n";
            }
        }
        output << "  }\n";
    }
    output << "}\n";
}
} // namespace lir
