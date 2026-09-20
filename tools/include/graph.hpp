#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

template <typename T>
concept Copyable = std::is_void_v<T> || (std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>);

namespace graph {
namespace detail {
struct empty_data {};

template <typename T> using data_storage_t = std::conditional_t<std::is_void_v<T>, empty_data, T>;

template <typename T, typename... Targs>
concept DataConstructible =
    (std::is_void_v<T> && sizeof...(Targs) == 0) || (!std::is_void_v<T> && std::is_constructible_v<T, Targs...>);
} // namespace detail

template <Copyable Tnode_data, Copyable Tedge_data> class orgraph_t {
  public:
    class node_t;
    class edge_t;

    using node_iterator = std::list<node_t>::iterator;
    using edge_iterator = std::list<edge_t>::iterator;
    using const_node_iterator = typename std::list<node_t>::const_iterator;
    using const_edge_iterator = typename std::list<edge_t>::const_iterator;

    class edge_t {
        friend class orgraph_t;
        using data_type = detail::data_storage_t<Tedge_data>;

      private:
        [[no_unique_address]] data_type edge_data;
        node_iterator prev;
        node_iterator next;

      public:
        edge_t(data_type data_, node_iterator prev_, node_iterator next_)
            requires(!std::is_void_v<Tedge_data>)
            : edge_data(data_), prev(prev_), next(next_) {}

        template <typename... Targs>
            requires detail::DataConstructible<Tedge_data, Targs...>
        edge_t(std::in_place_t, node_iterator prev_, node_iterator next_, Targs &&...args)
            : edge_data(std::forward<Targs>(args)...), prev(prev_), next(next_) {}

        node_iterator &prev_node() noexcept { return prev; }
        node_iterator &next_node() noexcept { return next; }

        const_node_iterator prev_node() const noexcept { return prev; }
        const_node_iterator next_node() const noexcept { return next; }

        data_type &data() noexcept
            requires(!std::is_void_v<Tedge_data>)
        {
            return edge_data;
        }
        const data_type &data() const noexcept
            requires(!std::is_void_v<Tedge_data>)
        {
            return edge_data;
        }
    };

    class node_t {
        friend class orgraph_t;
        using data_type = detail::data_storage_t<Tnode_data>;

        [[no_unique_address]] data_type node_data;
        std::list<edge_iterator> in;
        std::list<edge_iterator> out;

      public:
        node_t(data_type data_)
            requires(!std::is_void_v<Tnode_data>)
            : node_data(data_) {}

        template <typename... Targs>
            requires detail::DataConstructible<Tnode_data, Targs...>
        node_t(std::in_place_t, Targs &&...args) : node_data(std::forward<Targs>(args)...) {}

        auto in_begin() noexcept { return in.begin(); }
        const auto in_begin() const noexcept { return in.begin(); }

        auto in_end() noexcept { return in.end(); }
        const auto in_end() const noexcept { return in.end(); }

        auto out_begin() noexcept { return out.begin(); }
        const auto out_begin() const noexcept { return out.begin(); }

        auto out_end() noexcept { return out.end(); }
        const auto out_end() const noexcept { return out.end(); }

        data_type &data() noexcept
            requires(!std::is_void_v<Tnode_data>)
        {
            return node_data;
        }
        const data_type &data() const noexcept
            requires(!std::is_void_v<Tnode_data>)
        {
            return node_data;
        }

        void push_in_edge(edge_iterator edge) { in.push_back(edge); }
        void push_out_edge(edge_iterator edge) { out.push_back(edge); }
    };

  private:
    std::list<node_t> nodes;
    std::list<edge_t> edges;

  public:
    orgraph_t() = default;

    orgraph_t(const orgraph_t &other) {
        std::unordered_map<const node_t *, node_iterator> node_map;
        std::unordered_map<const edge_t *, edge_iterator> edge_map;

        for (const auto &node : other.nodes) {
            if constexpr (std::is_void_v<Tnode_data>) {
                node_map.emplace(std::addressof(node), new_node());
            } else {
                node_map.emplace(std::addressof(node), new_node(node.data()));
            }
        }
        for (const auto &edge : other.edges) {
            const auto prev = node_map.at(std::addressof(*edge.prev));
            const auto next = node_map.at(std::addressof(*edge.next));
            if constexpr (std::is_void_v<Tedge_data>) {
                edge_map.emplace(std::addressof(edge), edges.emplace(edges.end(), std::in_place, prev, next));
            } else {
                edge_map.emplace(std::addressof(edge),
                                 edges.emplace(edges.end(), std::in_place, prev, next, edge.data()));
            }
        }
        for (const auto &node : other.nodes) {
            auto copy = node_map.at(std::addressof(node));
            for (auto edge : node.in) {
                copy->push_in_edge(edge_map.at(std::addressof(*edge)));
            }
            for (auto edge : node.out) {
                copy->push_out_edge(edge_map.at(std::addressof(*edge)));
            }
        }
    }

    orgraph_t &operator=(const orgraph_t &other) {
        if (this != std::addressof(other)) {
            orgraph_t copy(other);
            swap(copy);
        }
        return *this;
    }

    orgraph_t(orgraph_t &&other) noexcept { swap(other); }

    orgraph_t &operator=(orgraph_t &&other) noexcept {
        if (this != std::addressof(other)) {
            orgraph_t moved(std::move(other));
            swap(moved);
        }
        return *this;
    }

    virtual ~orgraph_t() = default;

    void swap(orgraph_t &other) noexcept {
        nodes.swap(other.nodes);
        edges.swap(other.edges);
    }

    friend void swap(orgraph_t &left, orgraph_t &right) noexcept { left.swap(right); }

  public:
    auto begin() noexcept { return nodes.begin(); }
    const auto begin() const noexcept { return nodes.begin(); }

    auto end() noexcept { return nodes.end(); }
    const auto end() const noexcept { return nodes.end(); }

    node_iterator new_node(const detail::data_storage_t<Tnode_data> &data)
        requires(!std::is_void_v<Tnode_data>)
    {
        return nodes.emplace(nodes.end(), std::in_place, data);
    }

    template <typename... Targs>
        requires detail::DataConstructible<Tnode_data, Targs...>
    node_iterator new_node(Targs &&...args) {
        return nodes.emplace(nodes.end(), std::in_place, std::forward<Targs>(args)...);
    }

    edge_iterator new_edge(detail::data_storage_t<Tedge_data> data_, node_iterator prev_, node_iterator next_)
        requires(!std::is_void_v<Tedge_data>)
    {
        return new_edge_impl(prev_, next_, data_);
    }

    template <typename... Targs>
        requires detail::DataConstructible<Tedge_data, Targs...>
    edge_iterator new_edge(node_iterator prev_, node_iterator next_, Targs &&...args) {
        return new_edge_impl(prev_, next_, std::forward<Targs>(args)...);
    }

    void set_prev_node(edge_iterator edge, node_iterator prev_node) noexcept {
        if (edge->prev == prev_node) {
            return;
        }
        auto &old_out = edge->prev->out;
        const auto position = std::find(old_out.begin(), old_out.end(), edge);
        if (position == old_out.end()) {
            return;
        }
        prev_node->out.splice(prev_node->out.end(), old_out, position);
        edge->prev = prev_node;
    }

    void set_next_node(edge_iterator edge, node_iterator next_node) noexcept {
        if (edge->next == next_node) {
            return;
        }
        auto &old_in = edge->next->in;
        const auto position = std::find(old_in.begin(), old_in.end(), edge);
        if (position == old_in.end()) {
            return;
        }
        next_node->in.splice(next_node->in.end(), old_in, position);
        edge->next = next_node;
    }

    void erase_edge(edge_iterator edge) noexcept {
        if (!has_unique_edge_links(edge)) {
            return;
        }
        auto &out = edge->prev->out;
        auto &in = edge->next->in;
        const auto out_position = std::find(out.begin(), out.end(), edge);
        const auto in_position = std::find(in.begin(), in.end(), edge);
        out.erase(out_position);
        in.erase(in_position);
        edges.erase(edge);
    }

    void erase_node(node_iterator node) noexcept {
        std::size_t incoming = 0;
        std::size_t outgoing = 0;
        for (auto edge = edges.begin(); edge != edges.end(); ++edge) {
            const bool enters = edge->next == node;
            const bool leaves = edge->prev == node;
            if (enters || leaves) {
                if (!has_unique_edge_links(edge)) {
                    return;
                }
                incoming += enters;
                outgoing += leaves;
            }
        }
        if (incoming != node->in.size() || outgoing != node->out.size()) {
            return;
        }

        while (!node->in.empty()) {
            erase_edge(node->in.front());
        }
        while (!node->out.empty()) {
            erase_edge(node->out.front());
        }
        nodes.erase(node);
    }

    virtual bool check() const {
        std::unordered_set<const node_t *> node_addresses;
        for (const auto &node : nodes) {
            node_addresses.insert(std::addressof(node));
        }

        struct edge_counts {
            std::size_t in = 0;
            std::size_t out = 0;
        };
        std::unordered_map<const edge_t *, edge_counts> counts;
        for (const auto &edge : edges) {
            const auto prev = edge.prev_node();
            const auto next = edge.next_node();
            if (prev == nodes.end() || next == nodes.end()) {
                return false;
            }
            if (!node_addresses.contains(std::addressof(*prev)) || !node_addresses.contains(std::addressof(*next))) {
                return false;
            }
            counts.emplace(std::addressof(edge), edge_counts{});
        }

        for (const auto &node : nodes) {
            for (auto it = node.in_begin(); it != node.in_end(); ++it) {
                if (*it == edges.end()) {
                    return false;
                }
                const auto *edge = std::addressof(**it);
                auto found = counts.find(edge);
                if (found == counts.end() || std::addressof(*edge->next_node()) != std::addressof(node) ||
                    ++found->second.in != 1) {
                    return false;
                }
            }
            for (auto it = node.out_begin(); it != node.out_end(); ++it) {
                if (*it == edges.end()) {
                    return false;
                }
                const auto *edge = std::addressof(**it);
                auto found = counts.find(edge);
                if (found == counts.end() || std::addressof(*edge->prev_node()) != std::addressof(node) ||
                    ++found->second.out != 1) {
                    return false;
                }
            }
        }
        for (const auto &[edge, count] : counts) {
            if (count.in != 1 || count.out != 1) {
                return false;
            }
        }
        return true;
    }

    template <typename Tnode_label, typename Tedge_label>
    void write_dot(std::ostream &output, Tnode_label node_label, Tedge_label edge_label) const {
        if (!check()) {
            throw std::logic_error("Cannot export an invalid graph");
        }

        std::unordered_map<const node_t *, std::string> ids;
        output << "digraph G {\n";
        std::size_t index = 0;
        for (const auto &node : nodes) {
            const auto id = "n" + std::to_string(index++);
            ids.emplace(std::addressof(node), id);
            std::string label;
            if constexpr (std::is_void_v<Tnode_data>) {
                label = std::string(std::invoke(node_label));
            } else {
                label = std::string(std::invoke(node_label, node.data()));
            }
            output << "    " << id << " [label=\"" << escape_dot(label) << "\"];\n";
        }
        for (const auto &edge : edges) {
            std::string label;
            if constexpr (std::is_void_v<Tedge_data>) {
                label = std::string(std::invoke(edge_label));
            } else {
                label = std::string(std::invoke(edge_label, edge.data()));
            }
            output << "    " << ids.at(std::addressof(*edge.prev_node())) << " -> "
                   << ids.at(std::addressof(*edge.next_node())) << " [label=\"" << escape_dot(label) << "\"];\n";
        }
        output << "}\n";
        if (!output) {
            throw std::ios_base::failure("Cannot write DOT output");
        }
    }

    template <typename Tnode_label, typename Tedge_label>
    void write_dot(const std::filesystem::path &path, Tnode_label node_label, Tedge_label edge_label) const {
        std::ostringstream content;
        write_dot(content, std::move(node_label), std::move(edge_label));
        std::ofstream output(path);
        if (!output) {
            throw std::ios_base::failure("Cannot open DOT file: " + path.string());
        }
        output << content.str();
        output.close();
        if (!output) {
            throw std::ios_base::failure("Cannot save DOT file: " + path.string());
        }
    }

  private:
    bool has_unique_edge_links(edge_iterator edge) const noexcept {
        const auto &out = edge->prev->out;
        const auto &in = edge->next->in;
        return std::count(out.begin(), out.end(), edge) == 1 && std::count(in.begin(), in.end(), edge) == 1;
    }

    template <typename... Targs>
    edge_iterator new_edge_impl(node_iterator prev_, node_iterator next_, Targs &&...args) {
        auto edge = edges.emplace(edges.end(), std::in_place, prev_, next_, std::forward<Targs>(args)...);
        try {
            prev_->push_out_edge(edge);
        } catch (...) {
            edges.erase(edge);
            throw;
        }
        try {
            next_->push_in_edge(edge);
        } catch (...) {
            prev_->out.pop_back();
            edges.erase(edge);
            throw;
        }
        return edge;
    }

    static std::string escape_dot(std::string_view text) {
        std::string result;
        for (std::size_t i = 0; i < text.size(); ++i) {
            switch (text[i]) {
            case '\\':
                result += "\\\\";
                break;
            case '"':
                result += "\\\"";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\n";
                if (i + 1 < text.size() && text[i + 1] == '\n') {
                    ++i;
                }
                break;
            default:
                if (static_cast<unsigned char>(text[i]) < 32 && text[i] != '\t') {
                    throw std::invalid_argument("Unsupported control character in DOT label");
                }
                result += text[i];
            }
        }
        return result;
    }
};
} // namespace graph
