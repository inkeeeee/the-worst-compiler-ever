#pragma once

#include "frontend/frontend.hpp"

#include <filesystem>
#include <vector>

namespace frontend_test_helpers {
using node_t = frontend::ast_tree_t::const_node_iterator;

std::vector<node_t> children(node_t node);
std::vector<node_t> nodes(const frontend::ast_t &ast, frontend::node_kind_t kind);
node_t only(const frontend::ast_t &ast, frontend::node_kind_t kind);
void check_invalid_programs(const std::filesystem::path &directory, frontend::error_kind_t expected);
}
