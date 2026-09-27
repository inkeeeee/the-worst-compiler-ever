#pragma once

#include "frontend/ast.hpp"

#include <filesystem>
#include <istream>
#include <string>
#include <string_view>

namespace frontend {
ast_t parse(std::istream &input, std::string source = "<input>");
ast_t parse_string(std::string_view text, std::string source = "<string>");
ast_t parse_file(const std::filesystem::path &path);
}
