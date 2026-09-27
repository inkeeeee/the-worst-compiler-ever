#include "graph.hpp"
#include "test_helpers.hpp"
#include "tree.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace {
template <typename Tnode, typename Tedge>
concept AcceptsGraph = requires { typename graph::orgraph_t<Tnode, Tedge>; };

template <typename T>
concept HasData = requires(T &value) { value.data(); };

static_assert(AcceptsGraph<int, void> && AcceptsGraph<void, int> && AcceptsGraph<void, void>);
static_assert(!AcceptsGraph<std::unique_ptr<int>, int>);
static_assert(!AcceptsGraph<int, std::unique_ptr<int>>);
static_assert(!HasData<graph::orgraph_t<void, void>::node_t>);
static_assert(!HasData<graph::orgraph_t<void, void>::edge_t>);
static_assert(std::is_same_v<decltype(std::declval<const graph::orgraph_t<int, int>::edge_t &>().data()), const int &>);

struct payload {
    int id;
    std::string name;
    payload(int id_, std::string name_) : id(id_), name(std::move(name_)) {}
};

struct copy_only {
    int value;
    explicit copy_only(int value_) : value(value_) {}
    copy_only(const copy_only &) = default;
    copy_only &operator=(const copy_only &) = default;
    copy_only(copy_only &&) = delete;
    copy_only &operator=(copy_only &&) = delete;
};

TEST(GraphPayload, ConstructsNodeAndEdgeDataFromSeveralArguments) {
    graph::orgraph_t<payload, payload> graph;
    auto a = graph.new_node(1, "parent"), b = graph.new_node(2, "child");
    auto edge = graph.new_edge(a, b, 10, "flow");
    EXPECT_EQ(a->data().name, "parent");
    EXPECT_EQ(b->data().id, 2);
    EXPECT_EQ(edge->data().id, 10);
    EXPECT_EQ(edge->data().name, "flow");
    EXPECT_TRUE(graph.check());
}

TEST(GraphPayload, DataCanBeDefaultConstructed) {
    graph::orgraph_t<int, std::string> graph;
    auto a = graph.new_node();
    auto edge = graph.new_edge(a, a);
    EXPECT_EQ(a->data(), 0);
    EXPECT_TRUE(edge->data().empty());
    EXPECT_TRUE(graph.check());
}

TEST(GraphPayload, DoesNotRequireMoveConstructibleData) {
    graph::orgraph_t<copy_only, copy_only> graph;
    copy_only value(1);
    auto a = graph.new_node(value), b = graph.new_node(copy_only{2});
    graph.new_edge(copy_only{10}, a, b);
    auto copied = graph;
    EXPECT_TRUE(copied.check());
    EXPECT_EQ(copied.begin()->data().value, 1);
    EXPECT_EQ((*copied.begin()->out_begin())->data().value, 10);
}

TEST(GraphPayload, ForwardsMoveOnlyConstructorArguments) {
    struct from_pointer {
        int value;
        explicit from_pointer(std::unique_ptr<int> pointer) : value(*pointer) {}
    };
    graph::orgraph_t<from_pointer, from_pointer> graph;
    auto a = graph.new_node(std::make_unique<int>(7));
    auto edge = graph.new_edge(a, a, std::make_unique<int>(11));
    EXPECT_EQ(a->data().value, 7);
    EXPECT_EQ(edge->data().value, 11);
    EXPECT_TRUE(graph.check());
}

TEST(GraphPayload, SupportsVoidData) {
    {
        graph::orgraph_t<void, int> graph;
        auto a = graph.new_node(), b = graph.new_node();
        auto edge = graph.new_edge(7, a, b);
        EXPECT_EQ(edge->data(), 7);
        EXPECT_TRUE(graph.check());
    }
    {
        graph::orgraph_t<int, void> graph;
        auto a = graph.new_node(1), b = graph.new_node(2);
        auto edge = graph.new_edge(a, b);
        EXPECT_EQ(edge->next_node()->data(), 2);
        EXPECT_TRUE(graph.check());
    }
    graph::orgraph_t<void, void> graph;
    auto a = graph.new_node(), b = graph.new_node();
    graph.new_edge(a, a);
    graph.new_edge(a, b);
    std::ostringstream dot;
    graph.write_dot(dot, [] { return "N"; }, [] { return "E"; });
    EXPECT_EQ(dot.str(), "digraph G {\n    n0 [label=\"N\"];\n    n1 [label=\"N\"];\n"
                         "    n0 -> n0 [label=\"E\"];\n    n0 -> n1 [label=\"E\"];\n}\n");
    auto copy = graph;
    graph.erase_node(a);
    EXPECT_TRUE(graph.check());
    EXPECT_TRUE(copy.check());
    auto moved = std::move(copy);
    EXPECT_TRUE(moved.check());
    EXPECT_TRUE(copy.check());
    EXPECT_EQ(copy.begin(), copy.end());
}

TEST(GraphPayload, SupportsVoidTreeAndDetectsCycles) {
    tree::tree_t<void, void> tree;
    auto root = tree.new_node(), leaf = tree.new_node();
    tree.new_edge(root, leaf);
    EXPECT_TRUE(tree.check());
    EXPECT_EQ(tree.root(), root);
    auto back = tree.new_edge(leaf, root);
    EXPECT_FALSE(tree.check());
    tree.erase_edge(back);
    EXPECT_TRUE(tree.check());
}

} // namespace
