#include "graph.hpp"
#include "tree.hpp"
#include "test_helpers.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

namespace {
using graph_type = graph::orgraph_t<int, int>;

TEST(GraphLifetime, CopySurvivesDestructionOfOriginal) {
    std::unique_ptr<graph_type> copy;
    {
        graph_type original;
        auto a = original.new_node(1), b = original.new_node(2);
        original.new_edge(10, a, b);
        original.new_edge(20, a, a);
        copy = std::make_unique<graph_type>(original);
        copy->begin()->data() = 100;
        (*copy->begin()->out_begin())->data() = 200;
        EXPECT_EQ(a->data(), 1);
        EXPECT_EQ((*a->out_begin())->data(), 10);
        EXPECT_TRUE(original.check());
    }
    ASSERT_TRUE(copy->check());
    auto a = copy->begin();
    EXPECT_EQ(out_count(a), 2);
    auto edge = *a->out_begin();
    EXPECT_EQ(edge->next_node()->data(), 2);
    copy->erase_node(a);
    EXPECT_TRUE(copy->check());
    EXPECT_EQ(copy->begin()->data(), 2);
}

TEST(GraphLifetime, CopyAssignmentPreservesRewiredAdjacencyOrder) {
    graph_type original;
    auto a = original.new_node(1), b = original.new_node(2), c = original.new_node(3);
    auto first = original.new_edge(10, a, b);
    original.new_edge(20, a, b);
    original.new_edge(30, c, b);
    original.set_prev_node(first, c);
    original.set_next_node(first, c);
    original.set_next_node(first, b);
    graph_type copy;
    auto old_node = copy.new_node(99);
    copy.new_edge(990, old_node, old_node);
    copy = original;
    EXPECT_EQ(std::distance(copy.begin(), copy.end()), 3);
    auto copied_b = std::next(copy.begin()), copied_c = std::next(copied_b);
    std::vector<int> in, out;
    for (auto it = copied_b->in_begin(); it != copied_b->in_end(); ++it) {
        in.push_back((*it)->data());
    }
    for (auto it = copied_c->out_begin(); it != copied_c->out_end(); ++it) {
        out.push_back((*it)->data());
    }
    EXPECT_EQ(in, (std::vector<int>{20, 30, 10}));
    EXPECT_EQ(out, (std::vector<int>{30, 10}));
    EXPECT_TRUE(copy.check());
}

TEST(GraphLifetime, SelfCopyAndSelfMovePreserveIterators) {
    graph_type graph;
    auto node = graph.new_node(1);
    auto edge = graph.new_edge(10, node, node);
    graph = graph;
    ASSERT_TRUE(graph.check());
    graph = std::move(graph);
    EXPECT_TRUE(graph.check());
    EXPECT_EQ(graph.begin(), node);
    EXPECT_EQ(*node->out_begin(), edge);
}

TEST(GraphLifetime, MoveKeepsNodeAndEdgeIterators) {
    graph_type source;
    auto a = source.new_node(1), b = source.new_node(2);
    auto edge = source.new_edge(10, a, b);
    graph_type moved(std::move(source));
    EXPECT_TRUE(source.check());
    EXPECT_EQ(source.begin(), source.end());
    EXPECT_TRUE(moved.check());
    EXPECT_EQ(moved.begin(), a);
    EXPECT_EQ(*a->out_begin(), edge);

    graph_type destination;
    auto old_node = destination.new_node(99);
    destination.new_edge(990, old_node, old_node);
    destination = std::move(moved);
    EXPECT_TRUE(moved.check());
    EXPECT_EQ(moved.begin(), moved.end());
    EXPECT_EQ(destination.begin(), a);
    EXPECT_EQ(*a->out_begin(), edge);
    EXPECT_TRUE(destination.check());
    destination.erase_node(b);
    EXPECT_TRUE(destination.check());
    source.new_node(3);
    EXPECT_TRUE(source.check());
}

TEST(GraphLifetime, DerivedTreeCopiesAndMovesItsRootCorrectly) {
    tree::tree_t<int, int> source;
    auto root = source.new_node(1), child = source.new_node(2);
    source.new_edge(10, root, child);
    auto copied = source;
    ASSERT_TRUE(copied.check());
    EXPECT_NE(std::addressof(*source.root()), std::addressof(*copied.root()));
    auto copy_root = copied.root();
    auto moved = std::move(copied);
    EXPECT_TRUE(moved.check());
    EXPECT_EQ(moved.root(), copy_root);
    EXPECT_TRUE(copied.check());
    EXPECT_EQ(copied.root(), copied.end());
}

}
