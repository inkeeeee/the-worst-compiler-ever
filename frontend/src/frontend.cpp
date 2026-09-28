#include "frontend/frontend.hpp"
#include "frontend/driver.hpp"
#include "frontend/lexer.hpp"
#include "parser.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace frontend {
ast_t parse(std::istream &input, std::string source) {
    if (!input) {
        throw std::ios_base::failure("Cannot read source: " + source);
    }
    driver_t driver{std::move(source), {}};
    lexer_t lexer(input, driver.source);
    parser_t parser(lexer, driver);
    if (parser.parse() != 0 || !driver.ast.check()) {
        throw std::logic_error("Parser produced an invalid AST");
    }
    if (input.bad()) {
        throw std::ios_base::failure("Cannot read source: " + driver.source);
    }
    return std::move(driver.ast);
}

ast_t parse_string(std::string_view text, std::string source) {
    std::istringstream input{std::string(text)};
    return parse(input, std::move(source));
}

ast_t parse_file(const std::filesystem::path &path) {
    if (path.extension() != ".pickme") {
        throw std::invalid_argument("Source file must have the .pickme extension: " + path.string());
    }
    std::ifstream input(path, std::ios::binary);
    return parse(input, path.string());
}
} // namespace frontend
