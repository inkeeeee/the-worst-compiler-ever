#include "frontend/frontend.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>

namespace {
using frontend::semantic_result_t;
using T = frontend::type_kind_t;
using K = frontend::node_kind_t;

semantic_result_t analyze(const std::string &text) {
    return frontend::analyze(frontend::parse_string(text, "semantic.pickme"), "semantic.pickme");
}
std::string diagnostics(const semantic_result_t &result) {
    std::ostringstream output;
    result.write_diagnostics(output);
    return output.str();
}
const frontend::symbol_info_t &symbol(const semantic_result_t &result, const std::string &name) {
    const auto found = std::find_if(result.symbols.begin(), result.symbols.end(),
                                    [&](const auto &entry) { return entry.name == name; });
    if (found == result.symbols.end()) throw std::logic_error("Missing symbol: " + name);
    return *found;
}

class ValidSemantics : public testing::TestWithParam<const char *> {};
TEST_P(ValidSemantics, AcceptsProgram) {
    const auto result = analyze(GetParam());
    EXPECT_TRUE(result.ok()) << diagnostics(result);
}
INSTANTIATE_TEST_SUITE_P(Programs, ValidSemantics, testing::Values(
    "",
    "int main(void) { return 0; }",
    "void f() { return; }",
    "int x; int f() { x = 1; return x; }",
    "int f(int x) { { char x = 'a'; x++; } return x; }",
    "int f() { { int x = 1; } { bool x = true; } return 0; }",
    "int f() { int x = 1, y = x + 2; return y; }",
    "int f() { for (int i = 0; i < 2; ++i) { int j = i; } int i = 4; return i; }",
    "int f() { while (1) { for (;;) { break; } continue; } return 0; }",
    "int f(int x); int f(int y); int f(int z) { return z; }",
    "signed int f(unsigned int x); int f(unsigned x) { return x; }",
    "int f(int n) { if (n) return f(n - 1); return 0; }",
    "int g(); int f() { return g(); } int g() { return 0; }",
    "int f(int a[2]); int f(int *b) { return b[0]; }",
    "int f(int matrix[][3]) { return matrix[0][1]; }",
    "int f() { int x = 1; int *p = &x; *p = 3; p += 1; --p; return *p; }",
    "int f() { int a[2] = {1, 2}; int *p = a; return p[1] + 0[p]; }",
    "int a[2][3]; int *b[2]; int (*p)[3] = &a[0]; int *(*q)[2] = &b;",
    "int a[] = {1,2,3}; char s[] = \"a\\n\"; char exact[2] = \"ab\";",
    "int a[1 + 2 * 3]; int b[-(-2)]; int c['A' - 'A' + 1];",
    "int f() { int x = 0; void *v = &x; int *p = v; p = 0; if (p == 0) return 1; return p != v; }",
    "int f() { int a[4]; int *p = a; int *q = a + 3; long d = q - p; return d; }",
    "bool f() { int x; int *p = &x; bool b = p; return b && !p; }",
    "int f() { short a; char b; unsigned int c; long d; return a + b + c + d; }",
    "int f() { int x = {1}; int a[2][2] = {{1}, {2,3}}; return a[1][1] + x; }",
    "int f() { return 08 + 077; }",
    "void g(); void f() { g(); }",
    "int g(int n) { return n; } int f() { return (*g)(1); }",
    "int *f(int *p) { return p; }",
    "int f() { int a; int *p = &a; int **q = &p; return **q; }",
    "int f() { int x; int *p = &x; return p && x || !p; }",
    "int f() { unsigned long a; long long b; a + b; return 0; }",
    "int f() { int x; int *p = &x; p = 2 - 2; return 0; }",
    "int f() { for (;;) break; return 0; }",
    "int f() { int x = 1; x += 2; x -= 2; x *= 2; x /= 2; x %= 2; x <<= 1; x >>= 1; x &= 3; x |= 1; x ^= 2; return x; }"
));

struct invalid_case_t { const char *source; const char *message; };
class InvalidSemantics : public testing::TestWithParam<invalid_case_t> {};
TEST_P(InvalidSemantics, RejectsProgramWithDiagnostic) {
    const auto &example = GetParam();
    const auto result = analyze(example.source);
    EXPECT_FALSE(result.ok());
    const auto text = diagnostics(result);
    EXPECT_NE(text.find(example.message), std::string::npos) << text;
    EXPECT_NE(text.find("semantic.pickme:"), std::string::npos) << text;
}
INSTANTIATE_TEST_SUITE_P(Programs, InvalidSemantics, testing::Values(
    invalid_case_t{"int f() { return missing; }", "undeclared identifier"},
    invalid_case_t{"int f() { x = 1; int x; return x; }", "undeclared identifier"},
    invalid_case_t{"int f() { return g(); } int g() { return 0; }", "undeclared identifier"},
    invalid_case_t{"int f() { return x; } int x;", "undeclared identifier"},
    invalid_case_t{"int f() { int x; int x; return 0; }", "duplicate declaration"},
    invalid_case_t{"int x; bool x;", "duplicate declaration"},
    invalid_case_t{"int f(int x, int x) { return x; }", "duplicate declaration"},
    invalid_case_t{"int f(int x) { int x; return x; }", "duplicate declaration"},
    invalid_case_t{"int f() { { int x; } return x; }", "undeclared identifier"},
    invalid_case_t{"int f() { if (1) { int x; } else { x = 2; } return 0; }", "undeclared identifier"},
    invalid_case_t{"int f() { for (int i=0; i<1; i++) {} return i; }", "undeclared identifier"},
    invalid_case_t{"int f(int param); int g() { return param; }", "undeclared identifier"},
    invalid_case_t{"int f(int x); int f(char *x);", "conflicting"},
    invalid_case_t{"int f(); void f();", "conflicting"},
    invalid_case_t{"int f() {return 1;} int f() {return 2;}", "redefinition"},
    invalid_case_t{"int f; int f() {return 0;}", "conflicting"},
    invalid_case_t{"void x;", "variable cannot have type void"},
    invalid_case_t{"int f(void x);", "parameter cannot have type void"},
    invalid_case_t{"void a[2];", "array element"},
    invalid_case_t{"int a[0];", "array size"},
    invalid_case_t{"int a[-1];", "array size"},
    invalid_case_t{"int n; int a[n];", "array size"},
    invalid_case_t{"int a[1/0];", "array size"},
    invalid_case_t{"int a[2147483647 + 1];", "array size"},
    invalid_case_t{"int a[(0 << 32) + 1];", "array size"},
    invalid_case_t{"int a[(1 << -1) + 1];", "array size"},
    invalid_case_t{"int a[];", "array bound is missing"},
    invalid_case_t{"int a[2][];", "array element"},
    invalid_case_t{"int f()[2];", "function cannot return"},
    invalid_case_t{"int a[2] = {1,2,3};", "too many elements"},
    invalid_case_t{"int a[2] = 1;", "array requires"},
    invalid_case_t{"char a[1] = \"ab\";", "too long"},
    invalid_case_t{"int x = {1,2};", "scalar initializer"},
    invalid_case_t{"int f() { int *p = 1; return 0; }", "invalid initializer"},
    invalid_case_t{"int f() { int x; int *p; p = x; return 0; }", "invalid assignment"},
    invalid_case_t{"int f() { char *p; int *q; p = q; return 0; }", "invalid assignment"},
    invalid_case_t{"int f() { int **p; void **q; p = q; return 0; }", "invalid assignment"},
    invalid_case_t{"int f() { int a[2], b[2]; a = b; return 0; }", "modifiable lvalue"},
    invalid_case_t{"int f() { 1 = 2; return 0; }", "modifiable lvalue"},
    invalid_case_t{"int f() { int a; &(a+1); return 0; }", "address-of"},
    invalid_case_t{"int f() { *1; return 0; }", "dereference"},
    invalid_case_t{"int f() { void *p; *p; return 0; }", "dereference"},
    invalid_case_t{"int f() { int *p; p * 2; return 0; }", "invalid operands"},
    invalid_case_t{"int f() { int *p; p + p; return 0; }", "invalid operands"},
    invalid_case_t{"int f() { int *p; char *q; p - q; return 0; }", "invalid operands"},
    invalid_case_t{"int f() { int *p; p %= 2; return 0; }", "invalid operands"},
    invalid_case_t{"int f() { void *p; p++; return 0; }", "modifiable integer"},
    invalid_case_t{"int f() { int a[2]; ++a; return 0; }", "modifiable integer"},
    invalid_case_t{"int f() { ++1; return 0; }", "modifiable integer"},
    invalid_case_t{"int f() { int a; ++(a++); return 0; }", "modifiable integer"},
    invalid_case_t{"int f() { int *p; ~p; return 0; }", "integer operand"},
    invalid_case_t{"int f() { int a; a[0]; return 0; }", "subscript requires"},
    invalid_case_t{"int f() { int *p; p[p]; return 0; }", "subscript requires"},
    invalid_case_t{"int f() { void *p; p[0]; return 0; }", "subscript requires"},
    invalid_case_t{"int f() { int x; return x(); }", "not a function"},
    invalid_case_t{"int g(int x); int f() { int g; return g(1); }", "not a function"},
    invalid_case_t{"int g(int x); int f() { return g(); }", "wrong number"},
    invalid_case_t{"int g(); int f() { return g(1); }", "wrong number"},
    invalid_case_t{"int g(int *p); int f() { return g(2); }", "argument 1"},
    invalid_case_t{"int f() { return; }", "must return a value"},
    invalid_case_t{"void f() { return 1; }", "void function"},
    invalid_case_t{"int *f() { return 1; }", "invalid return value"},
    invalid_case_t{"void g(); int f() { return g(); }", "invalid return value"},
    invalid_case_t{"void g(); int f() { if (g()) return 1; return 0; }", "condition"},
    invalid_case_t{"void g(); int f() { g() + 1; return 0; }", "invalid operands"},
    invalid_case_t{"int f() { break; return 0; }", "inside a loop"},
    invalid_case_t{"int f() { continue; return 0; }", "inside a loop"},
    invalid_case_t{"int f() { while(0) {} break; return 0; }", "inside a loop"},
    invalid_case_t{"int f() { return 999999999999999999999999; }", "out of range"}
));

TEST(Semantics, OriginalExamplesAndFixturesRemainValid) {
    for (const auto &entry : std::filesystem::directory_iterator(std::filesystem::path(FRONTEND_CASES_DIR) / "valid")) {
        if (entry.path().extension() != ".pickme") continue;
        SCOPED_TRACE(entry.path().filename().string());
        const auto result = frontend::analyze(frontend::parse_file(entry.path()), entry.path().string());
        EXPECT_TRUE(result.ok()) << diagnostics(result);
    }
    const auto result = frontend::analyze(frontend::parse_file(FRONTEND_EXAMPLE_PATH));
    EXPECT_TRUE(result.ok()) << diagnostics(result);
}

TEST(Semantics, PreservesPointerAndArrayStructureAndInfersBounds) {
    const auto result = analyze("int *a[3]; int (*b)[3]; int matrix[2][3]; int values[] = {1,2,3}; char s[] = \"a\\n\";");
    ASSERT_TRUE(result.ok()) << diagnostics(result);
    EXPECT_EQ(symbol(result, "a").type.str(), "array[3](pointer(int))");
    EXPECT_EQ(symbol(result, "b").type.str(), "pointer(array[3](int))");
    EXPECT_EQ(symbol(result, "matrix").type.str(), "array[2](array[3](int))");
    EXPECT_EQ(symbol(result, "values").type.extent, 3U);
    EXPECT_EQ(symbol(result, "s").type.extent, 3U);
}

TEST(Semantics, BindsIdentifiersToTheNearestDeclaration) {
    const auto result = analyze("int x; int f() { x; { char x; x; } x; return 0; }");
    ASSERT_TRUE(result.ok()) << diagnostics(result);
    std::vector<frontend::expression_info_t> uses;
    for (const auto &entry : result.expressions)
        if (entry.kind == K::identifier && entry.text == "x") uses.push_back(entry);
    ASSERT_EQ(uses.size(), 3U);
    ASSERT_TRUE(uses[0].symbol);
    ASSERT_TRUE(uses[1].symbol);
    EXPECT_EQ(uses[0].symbol, uses[2].symbol);
    EXPECT_NE(uses[0].symbol, uses[1].symbol);
    EXPECT_EQ(uses[0].type.kind, T::integer);
    EXPECT_EQ(uses[1].type.kind, T::character);
    for (const auto &scope : result.scopes) {
        if (scope.parent) { EXPECT_LT(*scope.parent, scope.id); }
    }
}

TEST(Semantics, TypesEveryExpressionAndIntermediateValue) {
    const auto ast = frontend::parse_string("int f(int a) { int *p = &a; return a + 2 * *p < 10 && a; }");
    const auto result = frontend::analyze(ast);
    ASSERT_TRUE(result.ok()) << diagnostics(result);
    std::size_t count = 0;
    for (auto node = ast.tree().begin(); node != ast.tree().end(); ++node) {
        if (node->data().kind >= K::identifier && node->data().kind != K::arguments) ++count;
    }
    EXPECT_EQ(result.expressions.size(), count);
    for (const auto &entry : result.expressions) {
        EXPECT_NE(entry.type.kind, T::error);
        if (entry.kind == K::binary && (entry.text == "<" || entry.text == "&&")) {
            EXPECT_EQ(entry.type.kind, T::boolean);
        }
        if (entry.kind == K::binary && (entry.text == "+" || entry.text == "*")) {
            EXPECT_EQ(entry.type.kind, T::integer);
        }
    }
}

TEST(Semantics, IntegerPromotionsAndMixedSignednessUseLP64) {
    const auto result = analyze("int f() { char c; short s; unsigned int u; long l; unsigned long ul; long long ll; c+s; u+l; ul+ll; c<<ll; return 0; }");
    ASSERT_TRUE(result.ok()) << diagnostics(result);
    std::vector<T> types;
    for (const auto &entry : result.expressions)
        if (entry.kind == K::binary) types.push_back(entry.type.kind);
    EXPECT_EQ(types, (std::vector<T>{T::integer, T::long_integer, T::unsigned_long_long, T::integer}));
}

TEST(Semantics, ResultOwnsItsDataAndAnalysisCanBeRepeated) {
    const auto ast = frontend::parse_string("int f(int x) { return x + 1; }");
    const auto first = frontend::analyze(ast);
    const auto second = frontend::analyze(ast);
    EXPECT_TRUE(first.ok());
    EXPECT_EQ(first.expressions.size(), second.expressions.size());
    const auto detached = analyze("int f() { return 1 + 2; }");
    std::ostringstream output;
    detached.write_symbols(output);
    detached.write_types(output);
    EXPECT_NE(output.str().find("function() -> int"), std::string::npos);
    EXPECT_NE(output.str().find("Binary +"), std::string::npos);
}

TEST(Semantics, CollectsIndependentErrorsWithExactPositions) {
    const auto result = analyze("int f() {\n  missing = 1;\n  break;\n  return;\n}");
    ASSERT_EQ(result.diagnostics.size(), 3U) << diagnostics(result);
    EXPECT_EQ(result.diagnostics[0].range.begin.line, 2U);
    EXPECT_EQ(result.diagnostics[0].range.begin.column, 3U);
    EXPECT_EQ(result.diagnostics[1].range.begin.line, 3U);
    EXPECT_EQ(result.diagnostics[2].range.begin.line, 4U);
}

TEST(Semantics, ArrayParameterAdjustmentPreservesInnerDimensions) {
    const auto result = analyze("int f(int a[][3]) { return a[0][0]; }");
    ASSERT_TRUE(result.ok()) << diagnostics(result);
    EXPECT_EQ(symbol(result, "f").type.parameters.at(0).str(), "pointer(array[3](int))");
}
} // namespace
