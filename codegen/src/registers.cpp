#include "registers.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

namespace codegen {
using O = lir::opcode_t;
using K = lir::operand_kind_t;
using regs_t = std::set<lir::reg_t>;

bool writes_result(O opcode) {
    switch (opcode) {
    case O::label:
    case O::st:
    case O::jump:
    case O::branch:
    case O::call:
    case O::ret:
    case O::trap:
        return false;
    default:
        return true;
    }
}

allocation_t allocate_registers(const lir::function_t &f) {
    if (f.register_count > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::overflow_error("C codegen: too many virtual registers");
    std::vector<regs_t> reads(f.operations.size()), writes(f.operations.size());
    for (std::size_t i = 0; i < f.operations.size(); ++i) {
        const auto &op = f.operations[i];
        for (std::size_t j = 0; j < op.operands.size(); ++j) {
            const auto a = op.operands[j];
            if (a.kind != K::reg || a.value < 2) continue;
            (j == 0 && writes_result(op.opcode) ? writes[i] : reads[i]).insert(a.value);
        }
    }

    std::vector<hir::basic_block_t> blocks;
    std::map<lir::id_t, std::size_t> indices;
    for (const auto &node : f.cfg) {
        indices[node.data().label] = blocks.size();
        blocks.push_back(node.data());
    }
    std::vector<std::vector<std::size_t>> successors(blocks.size());
    for (const auto &node : f.cfg)
        for (auto edge = node.out_begin(); edge != node.out_end(); ++edge)
            successors[indices.at(node.data().label)].push_back(indices.at((*edge)->next_node()->data().label));

    std::vector<regs_t> use(blocks.size()), def(blocks.size()), in(blocks.size()), out(blocks.size());
    for (std::size_t b = 0; b < blocks.size(); ++b)
        for (auto i = blocks[b].begin; i < blocks[b].end; ++i) {
            for (auto r : reads[i])
                if (!def[b].contains(r)) use[b].insert(r);
            def[b].insert(writes[i].begin(), writes[i].end());
        }
    bool changed;
    do {
        changed = false;
        for (std::size_t b = blocks.size(); b-- > 0;) {
            regs_t live;
            for (auto s : successors[b]) live.insert(in[s].begin(), in[s].end());
            out[b] = live;
            for (auto r : def[b]) live.erase(r);
            live.insert(use[b].begin(), use[b].end());
            if (live != in[b]) {
                in[b] = std::move(live);
                changed = true;
            }
        }
    } while (changed);

    std::vector<regs_t> interference(f.register_count);
    auto connect = [&](lir::reg_t a, lir::reg_t b) {
        if (a == b) return;
        interference[a].insert(b);
        interference[b].insert(a);
    };
    for (std::size_t b = 0; b < blocks.size(); ++b) {
        auto live = out[b];
        for (auto i = blocks[b].end; i-- > blocks[b].begin;) {
            for (auto w : writes[i]) {
                for (auto r : live) connect(w, r);
                live.erase(w);
            }
            // Both input values must coexist even when neither survives the op.
            for (auto a : reads[i])
                for (auto c : reads[i]) connect(a, c);
            live.insert(reads[i].begin(), reads[i].end());
        }
        // Also cover source locals with indeterminate incoming values.
        for (auto a : live)
            for (auto c : live) connect(a, c);
    }

    std::vector<lir::reg_t> order(f.register_count - 2);
    std::iota(order.begin(), order.end(), 2);
    std::stable_sort(order.begin(), order.end(),
                     [&](auto a, auto b) { return interference[a].size() > interference[b].size(); });
    allocation_t result;
    result.registers.assign(f.register_count, -1);
    result.registers[lir::sp] = machine_sp;
    result.registers[lir::bp] = machine_bp;
    std::vector<bool> assigned(f.register_count);
    std::set<unsigned> saved;
    for (auto v : order) {
        std::array<bool, register_count> occupied{};
        for (auto other : interference[v])
            if (assigned[other] && result.registers[other] >= 0)
                occupied[result.registers[other] - first_register] = true;
        const auto free = std::find(occupied.begin(), occupied.end(), false);
        if (free == occupied.end()) {
            result.registers[v] = -static_cast<int>(++result.spill_count);
        } else {
            const auto physical = first_register + static_cast<unsigned>(free - occupied.begin());
            result.registers[v] = static_cast<int>(physical);
            saved.insert(physical);
        }
        assigned[v] = true;
    }
    result.saved.assign(saved.begin(), saved.end());
    return result;
}
} // namespace codegen
