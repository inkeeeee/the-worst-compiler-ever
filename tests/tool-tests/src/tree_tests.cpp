#include "tree.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <type_traits>

namespace {
using tree_type = tree::tree_t<int, int>;

TEST(Tree, ChecksEmptySingleAndDisconnectedNodes) {
    tree_type tree;
    EXPECT_TRUE(tree.check());
    EXPECT_EQ(tree.root(), tree.end());
    auto root = tree.new_node(1);
    EXPECT_EQ(tree.root(), root);
    EXPECT_TRUE(tree.check());
    const auto &view = tree;
    static_assert(std::is_same_v<decltype(view.root()->data()), const int &>);
    EXPECT_EQ(view.root()->data(), 1);
    tree.new_node(2);
    const graph::orgraph_t<int, int> &base = tree;
    EXPECT_FALSE(base.check());
    EXPECT_EQ(tree.root(), tree.end());
    std::ostringstream dot;
    auto label = [](int value) { return std::to_string(value); };
    EXPECT_THROW(base.write_dot(dot, label, label), std::logic_error);
    EXPECT_TRUE(dot.str().empty());
}

TEST(Tree, FindsRootAndChecksBranchChanges) {
    tree_type tree;
    auto a = tree.new_node(2), root = tree.new_node(1);
    auto b = tree.new_node(3), c = tree.new_node(4);
    tree.new_edge(10, root, a);
    auto edge = tree.new_edge(20, root, b);
    tree.new_edge(30, a, c);
    EXPECT_EQ(tree.root(), root);
    EXPECT_TRUE(tree.check());
    tree.set_prev_node(edge, a);
    EXPECT_EQ(tree.root(), root);
    EXPECT_TRUE(tree.check());
    tree.erase_node(b);
    EXPECT_EQ(tree.root(), root);
    EXPECT_TRUE(tree.check());
    tree.erase_node(a);
    EXPECT_FALSE(tree.check());
    EXPECT_EQ(tree.root(), tree.end());
}

TEST(Tree, RejectsSelfLoop) {
    tree_type tree;
    auto node = tree.new_node(1);
    tree.new_edge(10, node, node);
    EXPECT_FALSE(tree.check());
    EXPECT_EQ(tree.root(), tree.end());
}

TEST(Tree, RejectsCycleEvenWhenAnotherComponentHasARoot) {
    tree_type tree;
    auto root = tree.new_node(1), leaf = tree.new_node(2), a = tree.new_node(3), b = tree.new_node(4);
    tree.new_edge(10, root, leaf);
    tree.new_edge(20, a, b);
    tree.new_edge(30, b, a);
    EXPECT_EQ(tree.root(), root);
    EXPECT_FALSE(tree.check());
}

TEST(Tree, RejectsTwoParentsEvenWithoutACycle) {
    tree_type tree;
    auto root = tree.new_node(1), a = tree.new_node(2), b = tree.new_node(3), leaf = tree.new_node(4);
    tree.new_edge(10, root, a);
    tree.new_edge(20, root, b);
    tree.new_edge(30, a, leaf);
    auto edge = tree.new_edge(40, b, leaf);
    EXPECT_FALSE(tree.check());
    tree.set_prev_node(edge, a);
    EXPECT_FALSE(tree.check());
}

TEST(Tree, LongChainDoesNotUseRecursiveTraversal) {
    tree_type tree;
    auto root = tree.new_node(0), previous = root;
    for (int i = 1; i < 10000; ++i) {
        auto node = tree.new_node(i);
        tree.new_edge(i, previous, node);
        previous = node;
    }
    EXPECT_EQ(tree.root(), root);
    EXPECT_TRUE(tree.check());
}

} // namespace
