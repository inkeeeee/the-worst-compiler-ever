#include "frontend/frontend.hpp"
#include "frontend_test_helpers.hpp"
#include "test_helpers.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using K = frontend::node_kind_t;
using frontend_test_helpers::check_invalid_programs;
using frontend_test_helpers::children;
using frontend_test_helpers::only;

TEST(Frontend, ValidPrograms) {
    EXPECT_TRUE(frontend::parse_file(FRONTEND_EXAMPLE_PATH).check());
    for (const auto &entry : std::filesystem::directory_iterator(std::filesystem::path(FRONTEND_CASES_DIR) / "valid")) {
        SCOPED_TRACE(entry.path().filename().string());
        EXPECT_NO_THROW({ EXPECT_TRUE(frontend::parse_file(entry.path()).check()); });
    }
}

TEST(Frontend, ChecksSourceFileExtension) {
    temporary_directory directory;
    for (const auto &[filename, accepted] : std::vector<std::pair<std::string, bool>>{
             {"program.pickme", true}, {"module.test.pickme", true},
             {"program.c", false}, {"program.txt", false}, {"program", false},
             {"program.PICKME", false}, {"program.pickme.c", false}, {".pickme", false}}) {
        SCOPED_TRACE(filename);
        const auto path = directory.path / filename;
        write_text_file(path, "int main() { return 0; }");
        if (accepted) {
            EXPECT_TRUE(frontend::parse_file(path).check());
        } else {
            EXPECT_THROW(frontend::parse_file(path), std::invalid_argument);
        }
    }
}

TEST(Frontend, AcceptsDecimalIntegersWithLeadingZeros) {
    for (const std::string literal : {"09", "08", "077", "000123", "00"}) {
        SCOPED_TRACE(literal);
        const auto ast = frontend::parse_string("int main() { return " + literal + "; }");
        EXPECT_TRUE(ast.check());
        EXPECT_EQ(only(ast, K::integer_literal)->data().text, literal);
    }
}

TEST(Frontend, LexicalErrors) {
    check_invalid_programs(std::filesystem::path(FRONTEND_CASES_DIR) / "lexical_errors",
                           frontend::error_kind_t::lexical);
}

TEST(Frontend, SyntaxErrors) {
    check_invalid_programs(std::filesystem::path(FRONTEND_CASES_DIR) / "syntax_errors",
                           frontend::error_kind_t::syntax);
}

TEST(Frontend, ArithmeticPrecedenceAndLeftAssociativity) {
    const auto ast = frontend::parse_string("int main() { return 10 - 3 - 2 * 4; }");
    const auto subtraction = children(only(ast, K::return_statement)).at(0);
    ASSERT_EQ(subtraction->data().text, "-");
    const auto operands = children(subtraction);
    EXPECT_EQ(operands.at(0)->data().text, "-");
    EXPECT_EQ(operands.at(1)->data().text, "*");
    const auto left = children(operands.at(0));
    EXPECT_EQ(left.at(0)->data().text, "10");
    EXPECT_EQ(left.at(1)->data().text, "3");
}

TEST(Frontend, BitwiseComparisonAndLogicalPrecedence) {
    const auto ast = frontend::parse_string("int main() { return a || b && c | d ^ e & f == g < h << i + j * k; }");
    auto node = children(only(ast, K::return_statement)).at(0);
    for (const std::string operation : {"||", "&&", "|", "^", "&", "==", "<", "<<", "+", "*"}) {
        ASSERT_EQ(node->data().text, operation);
        node = children(node).at(1);
    }
    EXPECT_EQ(node->data().text, "k");
}

TEST(Frontend, AssignmentIsRightAssociative) {
    const auto ast = frontend::parse_string("int main() { a = b += c; }");
    const auto outer = children(only(ast, K::expression_statement)).at(0);
    ASSERT_EQ(outer->data().text, "=");
    const auto nested = children(outer).at(1);
    ASSERT_EQ(nested->data().text, "+=");
    EXPECT_EQ(children(nested).at(1)->data().text, "c");
}

TEST(Frontend, ElseBelongsToNearestIf) {
    const auto ast = frontend::parse_string("int main() { if (a) if (b) return 1; else return 2; }");
    auto body = children(only(ast, K::function)).at(2);
    auto outer = children(body).at(0);
    ASSERT_EQ(outer->data().kind, K::if_statement);
    ASSERT_EQ(children(outer).size(), 2U);
    auto inner = children(outer).at(1);
    ASSERT_EQ(inner->data().kind, K::if_statement);
    EXPECT_EQ(children(inner).size(), 3U);
}

TEST(Frontend, ElseIfIsAnIfInsideElse) {
    const auto ast = frontend::parse_string("int main() { if (a) return 1; else if (b) return 2; else return 3; }");
    const auto outer = children(children(only(ast, K::function)).at(2)).at(0);
    ASSERT_EQ(children(outer).size(), 3U);
    const auto alternative = children(outer).at(2);
    EXPECT_EQ(alternative->data().kind, K::if_statement);
    EXPECT_EQ(children(alternative).size(), 3U);
}

TEST(Frontend, DistinguishesPointerToArrayAndArrayOfPointers) {
    const auto ast = frontend::parse_string("int *a[3]; int (*b)[3]; int **c; int matrix[2][3];");
    const auto declarations = children(ast.tree().root());
    ASSERT_EQ(declarations.size(), 4U);
    const auto a = children(children(declarations.at(0)).at(1)).at(0);
    const auto b = children(children(declarations.at(1)).at(1)).at(0);
    EXPECT_EQ(a->data().kind, K::pointer_declarator);
    EXPECT_EQ(children(a).at(0)->data().kind, K::array_declarator);
    EXPECT_EQ(b->data().kind, K::array_declarator);
    EXPECT_EQ(children(b).at(0)->data().kind, K::pointer_declarator);
    const auto c = children(children(declarations.at(2)).at(1)).at(0);
    EXPECT_EQ(children(c).at(0)->data().kind, K::pointer_declarator);
    const auto matrix = children(children(declarations.at(3)).at(1)).at(0);
    EXPECT_EQ(matrix->data().kind, K::array_declarator);
    EXPECT_EQ(children(matrix).at(0)->data().kind, K::array_declarator);
}

TEST(Frontend, DistinguishesCallArguments) {
    const auto ast = frontend::parse_string("int main() { f(a, b); }");
    const auto args = children(only(ast, K::arguments));
    ASSERT_EQ(args.size(), 2U);
    EXPECT_EQ(args.at(0)->data().text, "a");
    EXPECT_EQ(args.at(1)->data().text, "b");
}

TEST(Frontend, ForKeepsFourOrderedChildren) {
    const auto ast = frontend::parse_string("int main() { for (;;) break; }");
    const auto parts = children(only(ast, K::for_statement));
    ASSERT_EQ(parts.size(), 4U);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(parts[i]->data().kind, K::empty);
    }
    EXPECT_EQ(parts[3]->data().kind, K::break_statement);
}

TEST(Frontend, ReportsPositionsWithCrLfCommentsAndEndOfFile) {
    try {
        frontend::parse_string("int main() {\r\n  /* comment */\r\n  @\r\n}", "position.pickme");
        FAIL() << "Expected lexical error";
    } catch (const frontend::parse_error_t &error) {
        EXPECT_EQ(error.kind(), frontend::error_kind_t::lexical);
        EXPECT_EQ(error.range().begin.line, 3U);
        EXPECT_EQ(error.range().begin.column, 3U);
    }
    try {
        frontend::parse_string("int main() {\n", "eof.pickme");
        FAIL() << "Expected syntax error";
    } catch (const frontend::parse_error_t &error) {
        EXPECT_EQ(error.kind(), frontend::error_kind_t::syntax);
        EXPECT_EQ(error.range().begin.line, 2U);
        EXPECT_EQ(error.range().begin.column, 1U);
    }
}

TEST(Frontend, PreservesLiteralSpellingAndEscapesDotOutput) {
    const auto ast = frontend::parse_string(R"(int main() { prints("quote: \" slash: \\ newline: \n"); return '\0'; })");
    const auto literal = only(ast, K::string_literal);
    EXPECT_EQ(literal->data().text, R"("quote: \" slash: \\ newline: \n")");
    EXPECT_EQ(only(ast, K::character_literal)->data().text, "'\\0'");
    std::ostringstream output;
    ast.write_dot(output);
    EXPECT_TRUE(output.str().starts_with("digraph G {"));
    EXPECT_NE(output.str().find("\\\\n"), std::string::npos);
    EXPECT_NE(output.str().find("\\\""), std::string::npos);
}

TEST(Frontend, RepeatedParsingAfterFailureDoesNotRetainScannerState) {
    EXPECT_THROW(frontend::parse_string("int main() { /*"), frontend::parse_error_t);
    const auto ast = frontend::parse_string("int main() { return 0; }");
    EXPECT_TRUE(ast.check());
    EXPECT_EQ(only(ast, K::function)->data().range.begin.line, 1U);
    auto copy = ast;
    auto moved = std::move(copy);
    EXPECT_TRUE(moved.check());
    EXPECT_TRUE(ast.check());
}

TEST(Frontend, ParsesEmptyUnitAndRejectsBrokenInputStream) {
    EXPECT_TRUE(frontend::parse_string("").check());
    std::istringstream input;
    input.setstate(std::ios::badbit);
    EXPECT_THROW(frontend::parse(input), std::ios_base::failure);
}
}
