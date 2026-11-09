#include "lir/lir.hpp"

#include <algorithm>
#include <set>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace lir {
namespace {
using O = opcode_t;
using K = operand_kind_t;
class verifier_t {
    const translation_unit_t &unit_;
    std::vector<diagnostic_t> errors_;
    std::optional<id_t> fi_, oi_;
    void error(std::string message) { errors_.push_back({fi_, oi_, std::move(message)}); }
    bool symbol(id_t id, bool function) {
        if (id >= unit_.symbols.size() || unit_.symbols[id].function != function) {
            error("invalid symbol reference");
            return false;
        }
        return true;
    }
    bool instruction(const function_t &f, const operation_t &op, std::vector<reg_t> &reads,
                     std::vector<reg_t> &writes) {
        // r=register, v=register/immediate, s=value or symbol, l=label, c=call target.
        std::string shape;
        bool result = false;
        switch (op.opcode) {
        case O::label:
        case O::jump:
            shape = "l";
            break;
        case O::branch:
            shape = "rll";
            break;
        case O::call:
            shape = "c";
            reads.push_back(sp);
            break;
        case O::ret:
            reads.push_back(sp);
            break;
        case O::trap:
            break;
        case O::mov:
            shape = "rs";
            result = true;
            break;
        case O::ld:
            shape = "rri";
            result = true;
            break;
        case O::st:
            shape = "rvi";
            break;
        case O::neg:
        case O::bit_not:
            shape = "rv";
            result = true;
            break;
        case O::add:
        case O::sub:
        case O::mul:
        case O::div:
        case O::rem:
        case O::shl:
        case O::shr:
        case O::bit_and:
        case O::bit_or:
        case O::bit_xor:
        case O::eq:
        case O::ne:
        case O::lt:
        case O::le:
        case O::gt:
        case O::ge:
            shape = "rvv";
            result = true;
            break;
        default:
            error("unknown opcode");
            return false;
        }
        bool valid = true;
        for (std::size_t i = 0; i < op.operands.size(); ++i) {
            const auto a = op.operands[i];
            const char expected = i < shape.size() ? shape[i] : '-';
            const bool match = expected == '-'   ? a.kind == K::none
                               : expected == 'r' ? a.kind == K::reg
                               : expected == 'i' ? a.kind == K::immediate
                               : expected == 'l' ? a.kind == K::label
                               : expected == 'c' ? (a.kind == K::reg || a.kind == K::symbol)
                                                 : (a.kind == K::reg || a.kind == K::immediate ||
                                                    (expected == 's' && a.kind == K::symbol));
            if (!match) {
                error("wrong operand kind or arity");
                valid = false;
            }
            if (a.kind == K::reg) {
                if (a.value >= f.register_count) {
                    error("register out of range");
                    valid = false;
                } else if (result && i == 0)
                    writes.push_back(a.value);
                else
                    reads.push_back(a.value);
            }
            if (a.kind == K::symbol) {
                if (a.value >= unit_.symbols.size()) {
                    error("symbol out of range");
                    valid = false;
                } else if (op.opcode == O::call && !symbol(a.value, true))
                    valid = false;
                if (op.type != scalar_t::u64) {
                    error("symbol address requires u64");
                    valid = false;
                }
            }
        }
        try {
            width(op.type);
        } catch (const std::exception &) {
            error("invalid scalar type");
            valid = false;
        }
        for (auto r : writes)
            if (r == sp || r == bp)
                if (op.type != scalar_t::u64 ||
                    (op.opcode != O::mov && op.opcode != O::ld && op.opcode != O::add && op.opcode != O::sub)) {
                    error("invalid write to sp/bp");
                    valid = false;
                }
        return valid;
    }
    bool cfg(const function_t &f) {
        if (!f.cfg.check()) {
            error("invalid CFG topology");
            return false;
        }
        auto expected = f;
        try {
            build_cfg(expected);
        } catch (const std::exception &e) {
            error(e.what());
            return false;
        }
        if (expected.operations != f.operations) {
            error("missing labels or explicit transitions");
            return false;
        }
        const auto describe = [](const hir::cfg_t &graph) {
            std::vector<std::tuple<id_t, id_t, id_t>> blocks;
            std::multiset<std::tuple<id_t, hir::edge_kind_t, id_t>> edges;
            for (const auto &node : graph) {
                const auto &b = node.data();
                blocks.emplace_back(b.label, b.begin, b.end);
                for (auto it = node.out_begin(); it != node.out_end(); ++it) {
                    const auto &e = std::as_const(**it);
                    edges.emplace(b.label, e.data(), e.next_node()->data().label);
                }
            }
            return std::pair{blocks, edges};
        };
        if (describe(f.cfg) != describe(expected.cfg)) {
            error("CFG does not match instructions");
            return false;
        }
        return true;
    }
    void definitions(const function_t &f, const std::vector<std::vector<reg_t>> &reads,
                     const std::vector<std::vector<reg_t>> &writes) {
        std::vector<hir::basic_block_t> blocks;
        std::unordered_map<const hir::cfg_t::node_t *, id_t> indices;
        for (const auto &node : f.cfg) {
            indices[&node] = blocks.size();
            blocks.push_back(node.data());
        }
        if (blocks.empty()) return;
        std::vector<std::vector<id_t>> pred(blocks.size()), succ(blocks.size());
        for (const auto &node : f.cfg) {
            const auto i = indices.at(&node);
            for (auto it = node.out_begin(); it != node.out_end(); ++it) {
                const auto j = indices.at(&*std::as_const(**it).next_node());
                pred[j].push_back(i);
                succ[i].push_back(j);
            }
        }
        std::vector<bool> reached(blocks.size());
        std::vector<id_t> todo{0};
        while (!todo.empty()) {
            const auto i = todo.back();
            todo.pop_back();
            if (reached[i]) continue;
            reached[i] = true;
            for (auto j : succ[i]) todo.push_back(j);
        }
        std::vector<bool> entry(f.register_count);
        entry[sp] = entry[bp] = true;
        // Source locals may have indeterminate initial values, just as in HIR.
        // Compiler-generated temporaries still require a definition on every path.
        for (const auto &where : f.locations)
            if (!where.temporary && where.kind == location_kind_t::reg) entry[where.reg] = true;
        const std::vector<bool> top(f.register_count, true);
        std::vector<std::vector<bool>> in(blocks.size(), top), out(blocks.size(), top);
        bool changed;
        do {
            changed = false;
            for (id_t i = 0; i < blocks.size(); ++i)
                if (reached[i]) {
                    auto state = i == 0 ? entry : top;
                    for (auto p : pred[i])
                        if (reached[p])
                            for (reg_t r = 0; r < state.size(); ++r) state[r] = state[r] && out[p][r];
                    in[i] = state;
                    for (auto j = blocks[i].begin; j < blocks[i].end; ++j)
                        for (auto r : writes[j]) state[r] = true;
                    if (state != out[i]) {
                        out[i] = std::move(state);
                        changed = true;
                    }
                }
        } while (changed);
        for (id_t i = 0; i < blocks.size(); ++i)
            if (reached[i]) {
                auto state = in[i];
                for (auto j = blocks[i].begin; j < blocks[i].end; ++j) {
                    oi_ = j;
                    for (auto r : reads[j])
                        if (!state[r]) error("virtual register used before definition");
                    for (auto r : writes[j]) state[r] = true;
                }
            }
        oi_.reset();
    }

  public:
    explicit verifier_t(const translation_unit_t &unit) : unit_(unit) {}
    std::vector<diagnostic_t> run() {
        std::set<id_t> data_symbols, function_symbols;
        for (const auto &data : unit_.data) {
            symbol(data.symbol, false);
            if (!data_symbols.insert(data.symbol).second) error("duplicate data symbol");
            if (!data.size || !data.alignment || (data.alignment & (data.alignment - 1)) || data.alignment > 16)
                error("invalid data layout");
            std::uint64_t end = 0;
            for (const auto &value : data.values) {
                unsigned n = 0;
                try {
                    n = width(value.type);
                } catch (const std::exception &) {
                    error("invalid data scalar type");
                    continue;
                }
                if (value.offset < end || value.offset > data.size || n > data.size - value.offset)
                    error("data initializer overlap or bounds error");
                if (value.offset % n) error("misaligned data initializer");
                end = value.offset + n;
            }
        }
        for (id_t i = 0; i < unit_.functions.size(); ++i) {
            fi_ = i;
            const auto &f = unit_.functions[i];
            symbol(f.symbol, true);
            if (!function_symbols.insert(f.symbol).second) error("duplicate function symbol");
            bool safe = true;
            if (f.register_count < 2) {
                error("missing sp/bp registers");
                safe = false;
            }
            if (f.frame_size % 16) error("frame is not 16-byte aligned");
            std::set<id_t> objects;
            std::set<reg_t> registers;
            std::vector<std::pair<std::uint64_t, std::uint64_t>> slots;
            for (const auto &where : f.locations) {
                if (!objects.insert(where.object).second) error("duplicate storage map object");
                if (where.kind == location_kind_t::reg) {
                    if (where.reg < 2 || where.reg >= f.register_count) {
                        error("invalid storage register");
                        safe = false;
                    }
                    if (where.size > 8 || !where.size) error("object does not fit a register");
                    if (!registers.insert(where.reg).second) error("overlapping storage registers");
                } else if (where.kind == location_kind_t::stack) {
                    const auto depth = std::uint64_t{0} - static_cast<std::uint64_t>(where.offset);
                    if (where.offset >= 0 || !where.size || depth > f.frame_size || where.size > depth ||
                        !where.alignment || (where.alignment & (where.alignment - 1)) || where.alignment > 16 ||
                        depth % where.alignment)
                        error("invalid stack slot");
                    else
                        slots.emplace_back(depth - where.size, depth);
                } else
                    error("invalid storage kind");
            }
            std::sort(slots.begin(), slots.end());
            for (id_t j = 1; j < slots.size(); ++j)
                if (slots[j].first < slots[j - 1].second) error("overlapping stack slots");
            std::vector<std::vector<reg_t>> reads(f.operations.size()), writes(f.operations.size());
            for (id_t j = 0; j < f.operations.size(); ++j) {
                oi_ = j;
                if (!instruction(f, f.operations[j], reads[j], writes[j])) safe = false;
            }
            oi_.reset();
            if (safe && cfg(f)) definitions(f, reads, writes);
        }
        fi_.reset();
        for (id_t i = 0; i < unit_.symbols.size(); ++i)
            if (!unit_.symbols[i].function && !data_symbols.contains(i)) error("data symbol has no storage");
        if (unit_.initializer && (!symbol(*unit_.initializer, true) || !function_symbols.contains(*unit_.initializer)))
            error("initializer is not a defined function");
        return errors_;
    }
};
} // namespace
std::vector<diagnostic_t> verify(const translation_unit_t &unit) {
    return verifier_t(unit).run();
}
} // namespace lir
