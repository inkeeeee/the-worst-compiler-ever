#include "graph.hpp"
#include "test_helpers.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

namespace {
struct throwing_data {
    inline static int alive = 0;
    inline static int copies_until_throw = -1;
    int value;

    explicit throwing_data(int value_) : value(value_) {
        if (value < 0) {
            throw std::runtime_error("constructor failure");
        }
        ++alive;
    }

    throwing_data(const throwing_data &other) : value(other.value) {
        if (copies_until_throw == 0) {
            throw std::runtime_error("copy failure");
        }
        if (copies_until_throw > 0) {
            --copies_until_throw;
        }
        ++alive;
    }

    throwing_data &operator=(const throwing_data &) = default;
    ~throwing_data() { --alive; }
};

class GraphExceptions : public testing::Test {
  protected:
    void SetUp() override {
        ASSERT_EQ(throwing_data::alive, 0);
        throwing_data::copies_until_throw = -1;
    }
    void TearDown() override {
        throwing_data::copies_until_throw = -1;
        EXPECT_EQ(throwing_data::alive, 0);
    }
};

TEST_F(GraphExceptions, ConstructionFailurePreservesGraph) {
    graph::orgraph_t<throwing_data, throwing_data> graph;
    auto a = graph.new_node(1), b = graph.new_node(2);
    auto edge = graph.new_edge(a, b, 10);
    EXPECT_THROW(graph.new_node(-1), std::runtime_error);
    EXPECT_THROW(graph.new_edge(a, b, -1), std::runtime_error);
    EXPECT_EQ(throwing_data::alive, 3);
    EXPECT_EQ(std::distance(graph.begin(), graph.end()), 2);
    EXPECT_EQ(out_count(a), 1);
    EXPECT_EQ(in_count(b), 1);
    EXPECT_EQ(*a->out_begin(), edge);
    EXPECT_TRUE(graph.check());
}

TEST_F(GraphExceptions, FailedCopyConstructorDestroysPartialNodes) {
    using graph_type = graph::orgraph_t<throwing_data, int>;
    graph_type source;
    source.new_node(1);
    source.new_node(2);
    source.new_node(3);
    throwing_data::copies_until_throw = 1;
    EXPECT_THROW({ graph_type copy(source); }, std::runtime_error);
    EXPECT_EQ(throwing_data::alive, 3);
    EXPECT_TRUE(source.check());
}

TEST_F(GraphExceptions, FailedCopyAssignmentPreservesDestinationAndIterators) {
    graph::orgraph_t<int, throwing_data> source, destination;
    auto a = source.new_node(1), b = source.new_node(2);
    source.new_edge(a, b, 10);
    source.new_edge(b, a, 20);
    auto old_node = destination.new_node(99);
    auto old_edge = destination.new_edge(old_node, old_node, 990);
    throwing_data::copies_until_throw = 1;
    EXPECT_THROW(destination = source, std::runtime_error);
    EXPECT_EQ(throwing_data::alive, 3);
    EXPECT_EQ(destination.begin(), old_node);
    EXPECT_EQ(*old_node->out_begin(), old_edge);
    EXPECT_EQ(old_edge->data().value, 990);
    EXPECT_TRUE(source.check());
    EXPECT_TRUE(destination.check());
}

} // namespace
