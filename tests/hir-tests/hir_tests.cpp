#include "evaluator.hpp"
#include "../lir-tests/evaluator.hpp"
#include "frontend/frontend.hpp"
#include "hir/hir.hpp"

#include <filesystem>
#include <gtest/gtest.h>
#include <limits>
#include <sstream>

namespace {
using O = hir::opcode_t;
using A = hir::operand_t;
using T = frontend::type_kind_t;
hir::type_t type(T kind) {
    hir::type_t t;
    t.kind = kind;
    return t;
}
hir::translation_unit_t compile(std::string_view source) {
    const auto ast = frontend::parse_string(source);
    const auto sem = frontend::analyze(ast);
    if (!sem.ok()) {
        std::ostringstream out;
        sem.write_diagnostics(out);
        throw std::runtime_error(out.str());
    }
    auto unit = hir::lower(ast, sem);
    auto errors = hir::verify(unit);
    if (!errors.empty()) {
        std::ostringstream out;
        for (const auto &e : errors)
            out << e.function.value_or(999) << ':' << e.operation.value_or(999) << ' ' << e.message << '\n';
        throw std::runtime_error(out.str());
    }
    return unit;
}
bool has_error(const hir::translation_unit_t &u, std::string_view text) {
    for (const auto &e : hir::verify(u))
        if (e.message.find(text) != std::string::npos) return true;
    return false;
}

TEST(Structs, AcceptsNestedRecursiveAndScopedDefinitions) {
    for (const auto *source :
         {"struct Node; struct Node { int value; struct Node *next; }; struct Node head;",
          "struct A { int x; }; struct B { struct A a[2]; char *s; }; struct B b = {{{1},{2}}, 0};",
          "struct A { int x; }; int f() { struct A { char c; }; struct A a = {'x'}; return a.c; }",
          "struct { int x; } a, b; int f() { a = b; return a.x; }",
          "struct A { int x; }; struct A f(struct A a) { a.x++; return a; }",
          "struct A { int x; }; struct A f(); int g() { return f().x; }"}) {
        SCOPED_TRACE(source);
        EXPECT_NO_THROW(compile(source));
    }
}
TEST(Structs, RejectsInvalidLayoutsAndMemberUses) {
    for (const auto *source :
         {"struct S { struct S self; };",
          "struct S { struct S self[2]; };",
          "struct A; struct B { struct A a; };",
          "struct S { void x; };",
          "struct S { int x; int x; };",
          "struct S { int x = 1; };",
          "struct S {};",
          "struct S { int x; }; struct S { int y; };",
          "struct S; struct S x;",
          "struct S { int x; }; int f() { struct S a; return a.y; }",
          "struct S { int x; }; int f() { struct S a; return a->x; }",
          "struct S { int x; }; int f() { struct S *a; return a.x; }",
          "struct A { int x; }; struct B { int x; }; int f() { struct A a; struct B b; a=b; return 0; }",
          "struct S { int x; }; struct S s = {1,2};",
          "struct S { int x; }; struct S s = {1,{2}};",
          "struct S { struct S { int x; } field; };",
          "struct S; struct S f(); int main(){f(); return 0;}",
          "struct S; int f(struct S s); int g(struct S *p){return f(*p);}",
          "struct S; int g(struct S *p, struct S *q){*p=*q;return 0;}",
          "struct S { int x; }; struct S f(); int g() { f().x = 1; return 0; }",
          "struct S { int x; }; int f() { struct S a; { struct S { int x; }; struct S b; a=b; } return 0; }"}) {
        SCOPED_TRACE(source);
        EXPECT_FALSE(frontend::analyze(frontend::parse_string(source)).ok());
    }
}
TEST(Hir, PreservesAllExistingValidFixtures) {
    for (const auto &entry : std::filesystem::directory_iterator(std::filesystem::path(FRONTEND_CASES_DIR) / "valid")) {
        SCOPED_TRACE(entry.path().string());
        const auto ast = frontend::parse_file(entry.path());
        const auto sem = frontend::analyze(ast);
        ASSERT_TRUE(sem.ok());
        const auto unit = hir::lower(ast, sem);
        const auto errors = hir::verify(unit);
        for (const auto &e : errors) ADD_FAILURE() << e.message;
    }
}
struct execution_case {
    const char *source;
    int expected;
};
class HirExecution : public testing::TestWithParam<execution_case> {};
TEST_P(HirExecution, ExecutesGeneratedOperations) {
    const auto &c = GetParam();
    const auto u = compile(c.source);
    EXPECT_EQ(hir_evaluator(u).run(), c.expected);
    const auto low = lir::lower(u);
    for (const auto &error : lir::verify(low)) ADD_FAILURE() << error.message;
    EXPECT_EQ(lir_evaluator(low).run(), c.expected);
}
INSTANTIATE_TEST_SUITE_P(
    Programs, HirExecution,
    testing::Values(
        execution_case{"int main() { return 2 + 3 * 4; }", 14},
        execution_case{"int main() { char x = 127; x += 2; return x; }", -127},
        execution_case{"int main() { int x=4; return x++ + ++x; }", 10},
        execution_case{"int main() { int x=0; false && ++x; true || ++x; return x; }", 0},
        execution_case{"int main() { int x=0; true && ++x; false || ++x; return x; }", 2},
        execution_case{"int main() { int *p=0; return p && *p; }", 0},
        execution_case{
            "int hit(int *p) { ++*p; return 1; } int main() { int x=0; false && hit(&x); true || hit(&x); return x; }",
            0},
        execution_case{
            "int main() { int s=0; for(int i=0;i<8;++i) { if(i==2)continue; if(i==6)break; s+=i; } return s; }", 13},
        execution_case{"int main() { int i=0,s=0; while(i<5) { ++i; if(i==3)continue; s+=i; } return s; }", 12},
        execution_case{
            "int main() { int n=0; for(int i=0;i<3;++i) { for(int j=0;j<3;++j) { if(j==1)break; ++n; } } return n; }",
            3},
        execution_case{"int fact(int n) { if(n<2)return 1; return n*fact(n-1); } int main() { return fact(5); }", 120},
        execution_case{"int plus(int x){return x+1;} int apply(int cb(int n), int x){return cb(x);} int main(){return "
                       "apply(plus,4);}",
                       5},
        execution_case{"int f(int x){return x+2;} int main(){return (*f)(3);}", 5},
        execution_case{"int f(){return 4;} int main(){int f(); return f();}", 4},
        execution_case{"int main() { int a[3]={1,2,3}; int *p=a; p+=2; return *p + (p-a); }", 5},
        execution_case{"int main() { int a[2][3]; int (*p)[3]=a; int (*q)[]=p; return p-q; }", 0},
        execution_case{"int main() { int a[2][3]={{1},{2,3}}; return a[0][1]+a[1][1]+a[1][2]; }", 3},
        execution_case{"int main() { char a[5]=\"ab\"; return a[0]+a[2]+a[4]; }", 97},
        execution_case{"int main() { char a[2]=\"ab\"; return a[1]; }", 98},
        execution_case{"int main() { int a[2]={3,4}; int i=0; a[i++] += 2; return i*10+a[0]; }", 15},
        execution_case{"struct S{int x;int y;}; int main(){struct S a={2,3}; struct S b=a; b.x=9; return a.x+b.x+b.y;}",
                       14},
        execution_case{"struct S{int x;int y;}; struct S f(struct S s){s.x+=2;return s;} int main(){struct S a={3,4}; "
                       "struct S b=f(a); return a.x*10+b.x;}",
                       35},
        execution_case{"struct S{int x;}; struct S f(){struct S s={7};return s;} int main(){return f().x;}", 7},
        execution_case{"struct S{int x;}; struct B{struct S a[2];int y;}; int main(){struct B b={{{2},{3}},4}; struct "
                       "B *p=&b; p->a[1].x+=5; return b.a[0].x+b.a[1].x+p->y;}",
                       14},
        execution_case{
            "struct N{int x;struct N *next;}; int main(){struct N a={2,0}; struct N b={5,&a}; return b.next->x+b.x;}",
            7},
        execution_case{"struct S{int x;int y;}; int main(){struct S a[2]={{3},{4,5}}; return a[0].y+a[1].x+a[1].y;}",
                       9},
        execution_case{"int f(){return 3;} int a=f(); int b=a+2; int main(){return a*10+b;}", 35},
        execution_case{"struct S{int a;int b;}; struct S s={2,3}; int main(){return s.a+s.b;}", 5},
        execution_case{"int main(){int x=1; {int x=9; x++;} return x;}", 1},
        execution_case{"int x=10;int bump(){++x;return x;}int id(int x){return x;}int main(){int x=3;int "
                       "y=x;x=9;return id(y)*100+bump()*10+x;}",
                       419},
        execution_case{"int g=1;int change(){g=7;return 2;}int main(){return g+change()+g;}", 10},
        execution_case{"int set(int *p){*p=7;return 2;} int main(){int x=1;return x+set(&x)+x;}", 10},
        execution_case{"int pair(int a,int b){return a*10+b;} int main(){int x=1;return pair(x,++x);}", 12},
        execution_case{"int main(){int x;int *p=&x;*p=7;return x;}", 7},
        execution_case{
            "int fill(int *p){*p=7;return 0;} int main(){int x;bool b=false;if(b)x=3;else fill(&x);return x;}", 7},
        execution_case{"struct S{int x;};int main(){struct S s;s.x=9;struct S t=s;return t.x;}", 9},
        execution_case{"struct S{int x;};int set(struct S *p){p->x=9;return 0;} int first(struct S s,int n){return "
                       "s.x;} int main(){struct S s={2};return first(s,set(&s));}",
                       2},
        execution_case{"int set(int *p){*p=9;return 1;} int main(){int a[2]={set(&a[1]),0};return a[0]*10+a[1];}", 10},
        execution_case{
            "struct S{int x;int y;};int main(){struct S s={0,0};int a[2][2]={{0,0},{0}};return s.x+s.y+a[1][1];}", 0},
        execution_case{"int main(){int i=0;int sum=0;while(i<4){sum+=(i&&(i<2||i==3));++i;}return sum;}", 2},
        execution_case{"int f(int n){return n==0||(n>0&&f(n-1));}int main(){return f(5)*10+f(-1);}", 10},
        execution_case{
            "struct S{char s[4];int n;int *p;};int main(){struct S s={\"ab\",7,0};return s.s[1]+s.s[3]+s.n;}", 105},
        execution_case{"int main(){char a[2]={130,0};return a[0];}", -126},
        execution_case{"int main(){bool a[2]={2,-3};return a[0]+a[1];}", 2},
        execution_case{"int main(){return 4;} ", 4}, execution_case{"int main(){}", 0}));

TEST(Hir, SemanticNodeBindingsSurviveAstCopy) {
    const auto ast = frontend::parse_string("struct S {int x;}; int main(){struct S a={3}; return a.x;}");
    const auto sem = frontend::analyze(ast);
    const auto copied = ast;
    const auto u = hir::lower(copied, sem);
    EXPECT_TRUE(hir::verify(u).empty());
    EXPECT_EQ(hir_evaluator(u).run(), 3);
}
TEST(Hir, ExampleTraversesLinkedStructsAndReturnsACopy) {
    const auto ast = frontend::parse_file(HIR_EXAMPLE_PATH);
    const auto sem = frontend::analyze(ast);
    ASSERT_TRUE(sem.ok());
    const auto u = hir::lower(ast, sem);
    EXPECT_TRUE(hir::verify(u).empty());
    EXPECT_EQ(hir_evaluator(u).run(), 11);
}
TEST(Hir, CopiedUnitOwnsItsOperationsAndCfg) {
    hir::translation_unit_t copied;
    {
        const auto original = compile("int main(){int x=0; while(x<3) ++x; return x;}");
        copied = original;
    }
    EXPECT_TRUE(hir::verify(copied).empty());
    EXPECT_EQ(hir_evaluator(copied).run(), 3);
    std::ostringstream ir, dot;
    hir::write(copied, ir);
    hir::write_cfg_dot(copied, dot);
    EXPECT_NE(ir.str().find("branch"), std::string::npos);
    EXPECT_NE(dot.str().find("digraph G"), std::string::npos);
}
TEST(Hir, CallsTerminateBlocksAndHaveContinuationEdges) {
    auto u = compile("int g(){return 1;} int main(){return g()+g();}");
    int calls = 0;
    for (const auto &node : u.functions.back().cfg) {
        const auto &last = u.functions.back().operations.at(node.data().end - 1);
        if (last.opcode != O::call) continue;
        ++calls;
        ASSERT_EQ(std::distance(node.out_begin(), node.out_end()), 1);
        EXPECT_EQ((*node.out_begin())->data(), hir::edge_kind_t::continuation);
    }
    EXPECT_EQ(calls, 2);
}
TEST(Hir, DirectVariableAccessUsesMemoryWithoutLosingSnapshots) {
    const auto u = compile("int main(){int x=3;int y=x;x=9;return y*10+x;}");
    EXPECT_EQ(hir_evaluator(u).run(), 39);
    bool has_address = false, has_load = false, has_store = false;
    for (const auto &op : u.functions.front().operations) {
        has_address |= op.opcode == O::address;
        has_load |= op.opcode == O::load;
        has_store |= op.opcode == O::store;
    }
    EXPECT_TRUE(has_address);
    EXPECT_TRUE(has_load);
    EXPECT_TRUE(has_store);
}
TEST(Hir, ShortCircuitTruthTablesPreserveCallsAndBooleanValues) {
    for (const bool conjunction : {false, true}) {
        for (const int left : {0, 2}) {
            for (const int right : {0, -3}) {
                const std::string op = conjunction ? "&&" : "||";
                const auto source = "int hit(int *p,int v){++*p;return v;}"
                                    "int main(){int calls=0;bool result=hit(&calls," +
                                    std::to_string(left) + ")" + op + "hit(&calls," + std::to_string(right) +
                                    ");return calls*10+result;}";
                SCOPED_TRACE(source);
                const auto u = compile(source);
                const bool evaluate_right = conjunction ? left != 0 : left == 0;
                const bool result = conjunction ? left != 0 && right != 0 : left != 0 || right != 0;
                EXPECT_EQ(hir_evaluator(u).run(), (evaluate_right ? 20 : 10) + int(result));
            }
        }
    }
}
TEST(Hir, AllZeroInitializerDoesNotRewriteEveryField) {
    const auto u = compile("struct S{int a;int b;};int main(){struct S s={0,0};return 0;}");
    for (const auto &op : u.functions.front().operations) EXPECT_NE(op.opcode, O::field_address);
    EXPECT_EQ(hir_evaluator(u).run(), 0);
}
TEST(Hir, ArrayOfStructsHasItsActualInitializerAsOneConstant) {
    const auto u = compile("struct Node{int value;struct Node *next;};int main(){struct Node "
                           "nodes[3]={{2,0},{3,0},{5,0}};return nodes[0].value*100+nodes[1].value*10+nodes[2].value;}");
    EXPECT_EQ(hir_evaluator(u).run(), 235);
    ASSERT_EQ(u.constants.front().type.kind, T::array);
    ASSERT_TRUE(std::holds_alternative<hir::aggregate_constant_t>(u.constants.front().value));
    const std::string actual = "[{value = 2, next = null}, {value = 3, next = null}, {value = 5, next = null}]";
    std::ostringstream text, dot;
    hir::write(u, text);
    hir::write_cfg_dot(u, dot);
    for (const auto &output : {text.str(), dot.str()}) {
        EXPECT_NE(output.find(actual), std::string::npos);
        EXPECT_EQ(output.find("[{value = 0, next = null}"), std::string::npos);
    }
    unsigned stores = 0;
    for (const auto &operation : u.functions.front().operations) stores += operation.opcode == O::store;
    EXPECT_EQ(stores, 1u);
}
TEST(HirOutput, ConstantsAndObjectsAreSeparateFromInstructions) {
    const auto u = compile("int main(){int x=7;return x;}");
    std::ostringstream text, dot;
    hir::write(u, text);
    hir::write_cfg_dot(u, dot);
    EXPECT_NE(text.str().find("constants {\n  constant #0 : int = 7"), std::string::npos);
    EXPECT_NE(text.str().find("  objects {\n    %x."), std::string::npos);
    EXPECT_NE(dot.str().find("constants [shape=plain, label=<<TABLE"), std::string::npos);
    EXPECT_NE(dot.str().find("f0_objects [shape=plain, label=<<TABLE"), std::string::npos);
    EXPECT_NE(dot.str().find("<TD ALIGN=\"LEFT\">#0</TD><TD ALIGN=\"LEFT\">int</TD><TD ALIGN=\"LEFT\">7</TD>"),
              std::string::npos);
    EXPECT_EQ(text.str().find("local %"), std::string::npos);
    EXPECT_EQ(dot.str().find("local %"), std::string::npos);
}
TEST(HirOutput, LargePartialInitializerKeepsAnExactCompactZeroTail) {
    const auto u = compile("int main(){int a[100000]={7};return a[0]+a[99999];}");
    EXPECT_EQ(hir_evaluator(u).run(), 7);
    std::ostringstream text, dot;
    hir::write(u, text);
    hir::write_cfg_dot(u, dot);
    for (const auto &output : {text.str(), dot.str()}) {
        EXPECT_NE(output.find("concat([7], repeat(0, 99999))"), std::string::npos);
        EXPECT_LT(output.size(), 6000u);
    }
}
TEST(HirOutput, ShowsNestedValuesAndTypesInBothTextAndCfg) {
    const auto u = compile(
        "struct S{int n;bool ok;char c;int *p;int a[2];};int main(){struct S s={0,false,'\\0',0,{0,0}};return s.n;}");
    std::ostringstream text, dot;
    hir::write(u, text);
    hir::write_cfg_dot(u, dot);
    const auto fields = "{n = 0, ok = false, c = '\\0', p = null, a = [0, 0]}";
    EXPECT_NE(text.str().find("constant #0 : S@0 = " + std::string(fields)), std::string::npos);
    EXPECT_NE(text.str().find("= constant " + std::string(fields) + " (#0)"), std::string::npos);
    // DOT escapes the backslash in the displayed character literal once more.
    EXPECT_NE(dot.str().find("{n = 0, ok = false, c = '\\\\0', p = null, a = [0, 0]} (#0)"), std::string::npos);
    for (const auto &output : {text.str(), dot.str()}) {
        EXPECT_NE(output.find("ptr<int>"), std::string::npos);
        EXPECT_NE(output.find(".n"), std::string::npos);
        EXPECT_EQ(output.find("= zero"), std::string::npos);
    }
    EXPECT_NE(dot.str().find("shape=box"), std::string::npos);
    EXPECT_NE(dot.str().find("subgraph cluster_f0"), std::string::npos);
    EXPECT_NE(dot.str().find("\\l"), std::string::npos);
}
TEST(HirOutput, ShowsCallsGlobalsAndReadableEscapedStrings) {
    const auto u = compile(R"(int prints(char *s);int g=42;int main(){prints("a\"b\\c\n");return g;})");
    std::ostringstream text, dot;
    hir::write(u, text);
    hir::write_cfg_dot(u, dot);
    for (const auto &output : {text.str(), dot.str()}) {
        EXPECT_NE(output.find("extern @prints."), std::string::npos);
        EXPECT_NE(output.find("call @prints."), std::string::npos);
        EXPECT_NE(output.find("global %g."), std::string::npos);
        EXPECT_NE(output.find("= constant 42 (#"), std::string::npos);
        EXPECT_NE(output.find("initializer @$init."), std::string::npos);
    }
    EXPECT_NE(text.str().find(R"("a\"b\\c\n\000")"), std::string::npos);
    EXPECT_NE(dot.str().find(R"(\"a\\\"b\\\\c\\n\\000\")"), std::string::npos);
}
TEST(HirOutput, LargeArraysHaveAnExactBoundedRepresentation) {
    const auto u = compile("int a[100000];int main(){return 0;}");
    std::ostringstream text, dot;
    hir::write(u, text);
    hir::write_cfg_dot(u, dot);
    for (const auto &output : {text.str(), dot.str()}) {
        EXPECT_NE(output.find("repeat(0, 100000)"), std::string::npos);
        EXPECT_LT(output.size(), 3000u);
    }
}
TEST(HirOutput, ScopedNamesAndIndirectCallsStayUnambiguous) {
    const auto u = compile("struct S{int x;};int f(int x){return x;} int apply(int cb(int x)){return cb(2);} int "
                           "main(){struct S s={1};{struct S{int x;};struct S s={2};s.x++;}return apply(f)+s.x;}");
    std::ostringstream text, dot;
    hir::write(u, text);
    hir::write_cfg_dot(u, dot);
    for (const auto &output : {text.str(), dot.str()}) {
        EXPECT_NE(output.find("struct S@0"), std::string::npos);
        EXPECT_NE(output.find("struct S@1"), std::string::npos);
        EXPECT_NE(output.find("call_indirect %"), std::string::npos);
    }
    EXPECT_EQ(hir_evaluator(u).run(), 3);
}
TEST(HirVerifier, RejectsCorruptTypesReferencesAndCfg) {
    auto good = compile("int main(){return 2+3;}");
    {
        auto u = good;
        for (auto &op : u.functions[0].operations)
            if (op.opcode == O::add) op.operands[2] = A::object(u.objects.size() + 1);
        EXPECT_TRUE(has_error(u, "out of range"));
    }
    {
        auto u = good;
        for (auto &op : u.functions[0].operations)
            if (op.opcode == O::add) u.objects[op.operands[2].id].type = type(T::long_integer);
        EXPECT_TRUE(has_error(u, "arithmetic"));
    }
    {
        auto u = good;
        u.functions[0].cfg.new_node(hir::basic_block_t{999, 0, 1});
        EXPECT_TRUE(has_error(u, "CFG does not match"));
    }
    {
        auto u = good;
        u.functions[0].operations.back() = {O::jump, {A::label(999)}, {}, {}};
        EXPECT_TRUE(has_error(u, "undefined label"));
    }
    {
        auto u = good;
        u.functions[0].operations[1].arguments.push_back(0);
        EXPECT_TRUE(has_error(u, "only calls"));
    }
}
TEST(HirVerifier, ChecksConstantRangesAndSupportsFullUnsigned64Range) {
    hir::translation_unit_t u;
    u.constants.push_back({type(T::unsigned_long_long), std::numeric_limits<std::uint64_t>::max()});
    EXPECT_TRUE(hir::verify(u).empty());
    u.constants[0].type = type(T::long_long_integer);
    EXPECT_TRUE(has_error(u, "out of range"));
    u.constants[0] = {type(T::unsigned_integer), std::int64_t(-1)};
    EXPECT_TRUE(has_error(u, "out of range"));
    u.constants[0] = {type(T::integer), std::int64_t(2147483648LL)};
    EXPECT_TRUE(has_error(u, "out of range"));
    u.constants[0] = {type(T::boolean), std::int64_t(2)};
    EXPECT_TRUE(has_error(u, "out of range"));
}
TEST(HirVerifier, ChecksNestedAggregateConstantMembers) {
    const auto good = compile("struct S{int x;int y;};int main(){struct S a[1]={{2,3}};return a[0].x;}");
    ASSERT_TRUE(std::holds_alternative<hir::aggregate_constant_t>(good.constants.front().value));
    {
        auto u = good;
        auto &elements = std::get<hir::aggregate_constant_t>(u.constants.front().value);
        const auto duplicate = elements.front();
        elements.push_back(duplicate);
        EXPECT_TRUE(has_error(u, "too many members"));
    }
    {
        auto u = good;
        auto &elements = std::get<hir::aggregate_constant_t>(u.constants.front().value);
        auto &fields = std::get<hir::aggregate_constant_t>(elements.front().value);
        fields.front().type = type(T::long_integer);
        EXPECT_TRUE(has_error(u, "member type mismatch"));
    }
    {
        auto u = good;
        auto &elements = std::get<hir::aggregate_constant_t>(u.constants.front().value);
        auto &fields = std::get<hir::aggregate_constant_t>(elements.front().value);
        fields.front().value = std::int64_t{2147483648LL};
        EXPECT_TRUE(has_error(u, "out of range"));
    }
    {
        auto u = good;
        u.constants.front().type = type(T::integer);
        EXPECT_TRUE(has_error(u, "requires an array or struct"));
    }
}
TEST(HirVerifier, RequiresDefinitionsOnEveryReachablePath) {
    auto u = compile("int main(){return 1;}");
    auto &f = u.functions[0];
    const auto result = f.operations[1].operands[0].id;
    const auto flag = u.objects.size();
    u.objects.push_back({"flag", type(T::boolean), hir::storage_t::parameter, 0, {}});
    f.locals.push_back(flag);
    f.parameters.push_back(flag);
    u.objects[f.object].type.parameters.push_back(type(T::boolean));
    auto definition = f.operations[1];
    f.operations = {{O::label, {A::label(0)}, {}, {}},
                    {O::branch, {A::object(flag), A::label(1), A::label(2)}, {}, {}},
                    {O::label, {A::label(1)}, {}, {}},
                    definition,
                    {O::jump, {A::label(2)}, {}, {}},
                    {O::label, {A::label(2)}, {}, {}},
                    {O::return_value, {A::object(result)}, {}, {}}};
    hir::build_cfg(f);
    EXPECT_TRUE(has_error(u, "before definition"));
    f.operations.insert(f.operations.begin() + 1, definition);
    hir::build_cfg(f);
    EXPECT_TRUE(hir::verify(u).empty());
}
TEST(HirVerifier, RejectsWrongFieldIndexAndCrossFunctionUse) {
    auto u = compile("struct S{int x;}; int f(){return 3;} int main(){struct S a={2}; return a.x;}");
    auto bad = u;
    for (auto &op : bad.functions.back().operations)
        if (op.opcode == O::field_address) op.operands[2] = A::field(100);
    EXPECT_TRUE(has_error(bad, "field index"));
    auto &ret = u.functions.back().operations.back();
    ret.operands[0] = A::object(u.functions.front().locals.front());
    EXPECT_TRUE(has_error(u, "another function"));
}
TEST(HirVerifier, RebuildIsIdempotentAndKeepsParallelBranches) {
    auto u = compile("int main(){return 0;}");
    auto &f = u.functions[0];
    const auto n = f.operations.size();
    hir::build_cfg(f);
    hir::build_cfg(f);
    EXPECT_EQ(f.operations.size(), n);
    EXPECT_TRUE(hir::verify(u).empty());
    hir::function_t graph{0,
                          {},
                          {},
                          {{O::label, {A::label(0)}, {}, {}},
                           {O::branch, {A::object(0), A::label(1), A::label(1)}, {}, {}},
                           {O::label, {A::label(1)}, {}, {}},
                           {O::return_value, {}, {}, {}}},
                          {}};
    hir::build_cfg(graph);
    EXPECT_EQ(std::distance(graph.cfg.begin()->out_begin(), graph.cfg.begin()->out_end()), 2);
}
} // namespace
