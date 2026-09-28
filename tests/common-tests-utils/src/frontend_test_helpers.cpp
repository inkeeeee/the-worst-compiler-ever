#include "frontend_test_helpers.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace frontend_test_helpers {
std::vector<node_t> children(node_t node) {
    std::vector<node_t> result;
    for (auto edge = node->out_begin(); edge != node->out_end(); ++edge) {
        result.push_back(std::as_const(**edge).next_node());
    }
    return result;
}

std::vector<node_t> nodes(const frontend::ast_t &ast, frontend::node_kind_t kind) {
    std::vector<node_t> result;
    for (auto node = ast.tree().begin(); node != ast.tree().end(); ++node) {
        if (node->data().kind == kind) {
            result.push_back(node);
        }
    }
    return result;
}

node_t only(const frontend::ast_t &ast, frontend::node_kind_t kind) {
    auto found = nodes(ast, kind);
    if (found.size() != 1) {
        throw std::logic_error("Expected exactly one node");
    }
    return found.front();
}

void check_invalid_programs(const std::filesystem::path &directory, frontend::error_kind_t expected) {
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        SCOPED_TRACE(entry.path().filename().string());
        try {
            frontend::parse_file(entry.path());
            ADD_FAILURE() << "Invalid program was accepted";
        } catch (const frontend::parse_error_t &error) {
            EXPECT_EQ(error.kind(), expected);
            EXPECT_GT(error.range().begin.line, 0U);
            EXPECT_GT(error.range().begin.column, 0U);
            EXPECT_NE(std::string(error.what()).find(entry.path().filename().string()), std::string::npos);
        }
    }
}

}
