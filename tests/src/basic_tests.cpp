#include "graph.hpp"
#include "test_helpers.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <type_traits>

namespace {
using graph_type = graph::orgraph_t<int, int>;
using string_graph = graph::orgraph_t<std::string, std::string>;

auto number_label = [](const int &value) { return std::to_string(value); };
auto string_label = [](const std::string &value) { return value; };

TEST(BasicGraph, EmptyGraphIsValid) {
    graph_type graph;
    EXPECT_EQ(graph.begin(), graph.end());
    EXPECT_TRUE(graph.check());
}

TEST(BasicGraph, NodesHaveDataAndNoEdges) {
    graph_type graph;
    auto first = graph.new_node(10);
    auto second = graph.new_node(20);
    EXPECT_EQ(first->data(), 10);
    EXPECT_EQ(second->data(), 20);
    EXPECT_EQ(in_count(first), 0);
    EXPECT_EQ(out_count(first), 0);
    first->data() = 30;
    const auto &view = graph;
    static_assert(std::is_same_v<decltype(view.begin()->data()), const int &>);
    EXPECT_EQ(view.begin()->data(), 30);
    EXPECT_TRUE(graph.check());
}

TEST(BasicGraph, CanSearchNodesByCondition) {
    graph_type graph;
    graph.new_node(3);
    auto expected = graph.new_node(12);
    graph.new_node(7);
    auto found = std::find_if(graph.begin(), graph.end(), [](const auto &node) { return node.data() % 2 == 0; });
    EXPECT_EQ(found, expected);
}

TEST(BasicDot, EqualLabelsKeepDifferentNodeIds) {
    string_graph graph;
    auto a = graph.new_node("same"), b = graph.new_node("same");
    graph.new_edge("edge", a, b);
    std::ostringstream output;
    graph.write_dot(output, string_label, string_label);
    EXPECT_EQ(output.str(), "digraph G {\n    n0 [label=\"same\"];\n"
                            "    n1 [label=\"same\"];\n    n0 -> n1 [label=\"edge\"];\n}\n");
}

TEST(BasicDot, EscapesQuotesBackslashesAndLineBreaks) {
    string_graph graph;
    graph.new_node("узел a\"b\\N\nc\r\nd");
    std::ostringstream output;
    graph.write_dot(output, string_label, string_label);
    EXPECT_NE(output.str().find(R"(label="узел a\"b\\N\nc\nd")"), std::string::npos);
}

TEST(BasicDot, RejectsInvalidLabelsAndStreamErrors) {
    string_graph graph;
    auto node = graph.new_node(std::string("a\0b", 3));
    std::ostringstream output;
    EXPECT_THROW(graph.write_dot(output, string_label, string_label), std::invalid_argument);
    node->data() = "valid";
    output.setstate(std::ios::badbit);
    EXPECT_THROW(graph.write_dot(output, string_label, string_label), std::ios_base::failure);
}

TEST(BasicDot, WritesAFile) {
    temporary_directory directory;
    const auto file = directory.path / "graph.dot";
    graph_type graph;
    graph.new_node(7);
    ASSERT_NO_THROW(graph.write_dot(file, number_label, number_label));
    std::ifstream input(file);
    const std::string content{std::istreambuf_iterator<char>(input), {}};
    EXPECT_EQ(content, "digraph G {\n    n0 [label=\"7\"];\n}\n");
    EXPECT_THROW(graph.write_dot(directory.path / "missing_parent" / "graph.dot", number_label, number_label),
                 std::ios_base::failure);
}

TEST(BasicDot, LabelFailureDoesNotTruncateExistingFile) {
    temporary_directory directory;
    const auto file = directory.path / "graph.dot";
    {
        std::ofstream output(file);
        output << "previous contents";
    }
    graph_type graph;
    graph.new_node(7);
    auto bad_label = [](const int &) -> std::string { throw std::runtime_error("label failed"); };
    EXPECT_THROW(graph.write_dot(file, bad_label, number_label), std::runtime_error);
    std::ifstream input(file);
    const std::string content{std::istreambuf_iterator<char>(input), {}};
    EXPECT_EQ(content, "previous contents");
}

} // namespace
