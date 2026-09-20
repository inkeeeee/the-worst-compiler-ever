#include "graph.hpp"
#include "test_helpers.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <memory>
#include <vector>

namespace {
using graph_type = graph::orgraph_t<int, int>;

TEST(Graph, EdgeConnectsBothNodes) {
    graph_type graph;
    auto a = graph.new_node(1), b = graph.new_node(2);
    auto edge = graph.new_edge(10, a, b);
    EXPECT_EQ(edge->prev_node(), a);
    EXPECT_EQ(edge->next_node(), b);
    EXPECT_EQ(edge->data(), 10);
    edge->data() = 11;
    EXPECT_EQ(edge->data(), 11);
    const auto &view = *edge;
    EXPECT_EQ(view.prev_node(), a);
    EXPECT_EQ(view.next_node(), b);
    EXPECT_EQ(view.data(), 11);
    EXPECT_EQ(in_count(a), 0);
    EXPECT_EQ(out_count(a), 1);
    EXPECT_EQ(in_count(b), 1);
    EXPECT_EQ(out_count(b), 0);
    EXPECT_EQ(*a->out_begin(), edge);
    EXPECT_EQ(*b->in_begin(), edge);
    EXPECT_TRUE(graph.check());
}

TEST(Graph, SelfLoopsAndParallelEdgesAreValid) {
    graph_type graph;
    auto a = graph.new_node(1), b = graph.new_node(2);
    graph.new_edge(10, a, a);
    graph.new_edge(20, a, b);
    graph.new_edge(30, a, b);
    EXPECT_EQ(in_count(a), 1);
    EXPECT_EQ(out_count(a), 3);
    EXPECT_EQ(in_count(b), 2);
    EXPECT_TRUE(graph.check());
}

TEST(Graph, CanCreateAndRemoveASelfLoopByRewiring) {
    graph_type graph;
    auto a = graph.new_node(1), b = graph.new_node(2);
    auto edge = graph.new_edge(10, a, b);
    graph.set_next_node(edge, a);
    EXPECT_EQ(in_count(b), 0);
    EXPECT_EQ(in_count(a), 1);
    ASSERT_TRUE(graph.check());
    graph.set_prev_node(edge, b);
    graph.set_prev_node(edge, b);
    graph.set_next_node(edge, a);
    EXPECT_EQ(in_count(a), 1);
    EXPECT_EQ(out_count(a), 0);
    EXPECT_EQ(out_count(b), 1);
    EXPECT_TRUE(graph.check());
}

TEST(Graph, ErasingEdgeKeepsNodesAndOtherEdges) {
    graph_type graph;
    auto a = graph.new_node(1), b = graph.new_node(2);
    auto erased = graph.new_edge(10, a, b);
    auto kept = graph.new_edge(20, a, b);
    graph.erase_edge(erased);
    EXPECT_EQ(std::distance(graph.begin(), graph.end()), 2);
    EXPECT_EQ(out_count(a), 1);
    EXPECT_EQ(in_count(b), 1);
    EXPECT_EQ(*a->out_begin(), kept);
    EXPECT_EQ(kept->data(), 20);
    auto loop = graph.new_edge(30, a, a);
    graph.erase_edge(loop);
    EXPECT_EQ(in_count(a), 0);
    EXPECT_EQ(out_count(a), 1);
    EXPECT_TRUE(graph.check());
}

TEST(Graph, ErasingNodeRemovesIncomingOutgoingAndSelfLoopEdges) {
    graph_type graph;
    auto a = graph.new_node(1), b = graph.new_node(2), c = graph.new_node(3);
    graph.new_edge(10, a, b);
    graph.new_edge(20, b, c);
    graph.new_edge(30, b, b);
    graph.new_edge(40, a, b);
    auto kept = graph.new_edge(50, a, c);
    graph.erase_node(b);
    EXPECT_EQ(std::distance(graph.begin(), graph.end()), 2);
    EXPECT_EQ(out_count(a), 1);
    EXPECT_EQ(in_count(c), 1);
    EXPECT_EQ(*a->out_begin(), kept);
    EXPECT_EQ(*c->in_begin(), kept);
    EXPECT_TRUE(graph.check());
}

TEST(Graph, UnaffectedIteratorsSurviveMixedMutations) {
    graph_type graph;
    auto a = graph.new_node(1), b = graph.new_node(2), c = graph.new_node(3);
    auto kept = graph.new_edge(10, a, b);
    const auto *node_address = std::addressof(*a);
    const auto *edge_address = std::addressof(*kept);
    graph.new_edge(20, b, c);
    graph.new_edge(30, c, c);
    graph.erase_node(c);
    EXPECT_EQ(std::addressof(*a), node_address);
    EXPECT_EQ(std::addressof(*kept), edge_address);
    EXPECT_EQ(kept->data(), 10);
    EXPECT_TRUE(graph.check());
}

TEST(Graph, DepthFirstTraversalHandlesCyclesAndDisconnectedNodes) {
    graph_type graph;
    auto a = graph.new_node(1), b = graph.new_node(2), c = graph.new_node(3);
    graph.new_node(4);
    graph.new_edge(10, a, b);
    graph.new_edge(20, b, c);
    graph.new_edge(30, c, a);
    std::vector<graph_type::node_iterator> pending{b};
    std::set<int> visited;
    while (!pending.empty()) {
        auto node = pending.back();
        pending.pop_back();
        if (!visited.insert(node->data()).second) {
            continue;
        }
        for (auto edge = node->out_begin(); edge != node->out_end(); ++edge) {
            pending.push_back((*edge)->next_node());
        }
    }
    EXPECT_EQ(visited, (std::set<int>{1, 2, 3}));
    EXPECT_TRUE(graph.check());
}

TEST(Graph, ValidatorRejectsDuplicateAdjacencyEntry) {
    graph_type graph;
    auto a = graph.new_node(1), b = graph.new_node(2);
    auto first = graph.new_edge(10, a, b);
    graph.new_edge(20, a, b);
    *std::next(b->in_begin()) = first;
    EXPECT_FALSE(graph.check());
}

TEST(Graph, NoexceptMutationsIgnoreMissingLinks) {
    for (bool missing_incoming : {false, true}) {
        SCOPED_TRACE(missing_incoming);
        graph_type graph;
        auto a = graph.new_node(1), b = graph.new_node(2), c = graph.new_node(3);
        auto edge = graph.new_edge(10, a, b);
        if (missing_incoming) {
            edge->next_node() = c;
            graph.set_next_node(edge, b);
            EXPECT_EQ(edge->next_node(), c);
        } else {
            edge->prev_node() = c;
            graph.set_prev_node(edge, a);
            EXPECT_EQ(edge->prev_node(), c);
        }
        EXPECT_FALSE(graph.check());
        graph.erase_edge(edge);
        graph.erase_node(c);
        ASSERT_EQ(std::distance(graph.begin(), graph.end()), 3);
        ASSERT_EQ(out_count(a), 1);
        ASSERT_EQ(in_count(b), 1);
        EXPECT_EQ(*a->out_begin(), edge);
        EXPECT_EQ(*b->in_begin(), edge);
        EXPECT_EQ(in_count(c), 0);
        EXPECT_EQ(out_count(c), 0);
        EXPECT_EQ(edge->data(), 10);
        edge->prev_node() = a;
        edge->next_node() = b;
        EXPECT_TRUE(graph.check());
    }
}

TEST(Graph, ErasingEdgeDoesNothingWhenAnAdjacencyEntryIsDuplicated) {
    for (bool duplicate_incoming : {false, true}) {
        SCOPED_TRACE(duplicate_incoming);
        graph_type graph;
        auto a = graph.new_node(1), b = graph.new_node(2);
        auto edge = graph.new_edge(10, a, b);
        if (duplicate_incoming) {
            b->push_in_edge(edge);
        } else {
            a->push_out_edge(edge);
        }

        graph.erase_edge(edge);

        EXPECT_EQ(in_count(b), duplicate_incoming ? 2 : 1);
        EXPECT_EQ(out_count(a), duplicate_incoming ? 1 : 2);
        EXPECT_EQ(edge->data(), 10);
        EXPECT_FALSE(graph.check());
    }
}

TEST(Graph, ErasingNodeChecksAllIncidentEdgesBeforeRemovingAny) {
    graph_type graph;
    auto a = graph.new_node(1), b = graph.new_node(2);
    auto c = graph.new_node(3), d = graph.new_node(4);
    auto first = graph.new_edge(10, a, b);
    auto second = graph.new_edge(20, c, b);
    second->prev_node() = d;

    graph.erase_node(b);

    ASSERT_EQ(std::distance(graph.begin(), graph.end()), 4);
    EXPECT_EQ(in_count(b), 2);
    EXPECT_EQ(out_count(a), 1);
    EXPECT_EQ(out_count(c), 1);
    EXPECT_EQ(out_count(d), 0);
    EXPECT_EQ(first->data(), 10);
    EXPECT_EQ(second->data(), 20);
    second->prev_node() = c;
    EXPECT_TRUE(graph.check());
}

TEST(Graph, ErasingNodeDoesNothingWhenItListsAnUnrelatedEdge) {
    graph_type graph;
    auto a = graph.new_node(1), b = graph.new_node(2), c = graph.new_node(3);
    auto edge = graph.new_edge(10, a, b);
    c->push_out_edge(edge);

    graph.erase_node(c);

    ASSERT_EQ(std::distance(graph.begin(), graph.end()), 3);
    EXPECT_EQ(out_count(a), 1);
    EXPECT_EQ(out_count(c), 1);
    EXPECT_EQ(in_count(b), 1);
    EXPECT_EQ(edge->data(), 10);
    EXPECT_FALSE(graph.check());
}

}
