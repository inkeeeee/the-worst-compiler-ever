#pragma once

#include "graph.hpp"

#include <iterator>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tree {
template <Copyable Tnode_data, Copyable Tedge_data>
class tree_t : public graph::orgraph_t<Tnode_data, Tedge_data> {
    using base_t = graph::orgraph_t<Tnode_data, Tedge_data>;
    using node_type = typename std::iterator_traits<typename base_t::const_node_iterator>::value_type;

  public:
    using typename base_t::node_iterator;
    using typename base_t::const_node_iterator;

    node_iterator root() noexcept { return find_root(this->begin(), this->end()); }
    const_node_iterator root() const noexcept { return find_root(this->begin(), this->end()); }

    bool check() const override {
        if (!base_t::check()) {
            return false;
        }
        if (this->begin() == this->end()) {
            return true;
        }

        std::size_t node_count = 0;
        for (const auto &node : *this) {
            ++node_count;
            if (std::distance(node.in_begin(), node.in_end()) > 1) {
                return false;
            }
        }
        const auto first = root();
        if (first == this->end()) {
            return false;
        }

        std::unordered_set<const node_type *> visited;
        std::vector<const_node_iterator> pending{first};
        while (!pending.empty()) {
            const auto node = pending.back();
            pending.pop_back();
            if (!visited.insert(std::addressof(*node)).second) {
                return false;
            }
            for (auto edge = node->out_begin(); edge != node->out_end(); ++edge) {
                pending.push_back(std::as_const(**edge).next_node());
            }
        }
        return visited.size() == node_count;
    }

  private:
    template <typename Titerator>
    static Titerator find_root(Titerator first, Titerator last) noexcept {
        auto result = last;
        for (auto node = first; node != last; ++node) {
            if (node->in_begin() == node->in_end()) {
                if (result != last) {
                    return last;
                }
                result = node;
            }
        }
        return result;
    }
};
}
