#include "../hir-tests/evaluator.hpp"
#include "evaluator.hpp"
#include "frontend/frontend.hpp"

#include <filesystem>
#include <gtest/gtest.h>
#include <sstream>

namespace {
using O = lir::opcode_t;
using A = lir::operand_t;
using S = lir::scalar_t;
using T = frontend::type_kind_t;
hir::translation_unit_t high(std::string_view source) {
    const auto ast = frontend::parse_string(source);
    const auto sem = frontend::analyze(ast);
    if (!sem.ok()) {
        std::ostringstream text;
        sem.write_diagnostics(text);
        throw std::runtime_error(text.str());
    }
    return hir::lower(ast, sem);
}
lir::translation_unit_t compile(std::string_view source) {
    auto low = lir::lower(high(source));
    const auto errors = lir::verify(low);
    if (!errors.empty()) {
        std::ostringstream text;
        for (const auto &e : errors)
            text << e.function.value_or(999) << ':' << e.operation.value_or(999) << ' ' << e.message << '\n';
        throw std::runtime_error(text.str());
    }
    return low;
}
bool error_contains(const lir::translation_unit_t &u, std::string_view expected) {
    for (const auto &error : lir::verify(u))
        if (error.message.find(expected) != std::string::npos) return true;
    return false;
}
const lir::location_t &location(const lir::function_t &f, std::string_view name) {
    for (const auto &where : f.locations)
        if (where.name == name) return where;
    throw std::runtime_error("missing storage map entry");
}
TEST(Lir, ExistingFixturesAndLinkedNodeExample) {
    for (const auto &entry : std::filesystem::directory_iterator(std::filesystem::path(FRONTEND_CASES_DIR) / "valid")) {
        SCOPED_TRACE(entry.path());
        const auto ast = frontend::parse_file(entry.path());
        EXPECT_TRUE(lir::verify(lir::lower(hir::lower(ast, frontend::analyze(ast)))).empty());
    }
    const auto ast = frontend::parse_file(HIR_EXAMPLE_PATH);
    const auto low = lir::lower(hir::lower(ast, frontend::analyze(ast)));
    EXPECT_TRUE(lir::verify(low).empty());
    EXPECT_EQ(lir_evaluator(low).run(), 11);
}
TEST(LirStorage, ScalarObjectsBecomeRegistersAndKeepSnapshots) {
    const auto u = compile("int main(){int x=3;int y=x;x=9;return y*10+x;}");
    const auto &f = u.functions.front();
    EXPECT_EQ(f.frame_size, 0u);
    EXPECT_EQ(location(f, "x").kind, lir::location_kind_t::reg);
    EXPECT_EQ(location(f, "y").kind, lir::location_kind_t::reg);
    EXPECT_EQ(lir_evaluator(u).run(), 39);
}
TEST(LirStorage, ExplicitAddressRequiresStackEvenForAnUnusedPointer) {
    for (const auto *source : {"int main(){int x=7;&x;return x;}", "int main(){int x=3;*(&x)=7;return x;}"}) {
        const auto u = compile(source);
        EXPECT_EQ(location(u.functions.front(), "x").kind, lir::location_kind_t::stack);
        EXPECT_EQ(lir_evaluator(u).run(), 7);
    }
}
TEST(LirStorage, EscapingHirAddressRequiresMemoryWithoutFrontendMetadata) {
    auto h = high("int set(int *p){*p=9;return 0;}int main(){int x=1;set(&x);return x;}");
    for (auto &object : h.objects) object.address_taken = false;
    const auto u = lir::lower(h);
    EXPECT_TRUE(lir::verify(u).empty());
    EXPECT_EQ(location(u.functions.back(), "x").kind, lir::location_kind_t::stack);
    EXPECT_EQ(lir_evaluator(u).run(), 9);
}
TEST(LirLayout, ComputesByteOffsetsAndTrailingPadding) {
    const auto h = high("struct S{char c;int i;short s;char d;};struct S g;int main(){return 0;}");
    const auto it = std::find_if(h.objects.begin(), h.objects.end(), [](const auto &o) { return o.name == "g"; });
    const auto info = lir::layout(it->type, h.structures);
    EXPECT_EQ(info.size, 12u);
    EXPECT_EQ(info.alignment, 4u);
    EXPECT_EQ(info.fields, (std::vector<std::uint64_t>{0, 4, 8, 10}));
    auto array = hir::type_t::array_of(it->type, 3);
    EXPECT_EQ(lir::layout(array, h.structures).size, 36u);
}
TEST(LirLayout, RejectsOverflowAndIncompleteLayout) {
    hir::type_t t;
    t.kind = T::long_integer;
    EXPECT_THROW(lir::layout(hir::type_t::array_of(t, std::numeric_limits<std::size_t>::max()), {}),
                 std::overflow_error);
    EXPECT_THROW(lir::layout(hir::type_t::array_of(t, {}), {}), std::invalid_argument);
    EXPECT_THROW(lir::align_up(std::numeric_limits<std::uint64_t>::max(), 8), std::overflow_error);
    EXPECT_THROW(lir::align_up(7, 3), std::invalid_argument);
}
struct program_t {
    const char *source;
    int expected;
};
class LirExecution : public testing::TestWithParam<program_t> {};
TEST_P(LirExecution, ExecutesBytesAndRestoresTheStack) {
    const auto &p = GetParam();
    EXPECT_EQ(hir_evaluator(high(p.source)).run(), p.expected);
    EXPECT_EQ(lir_evaluator(compile(p.source)).run(), p.expected);
}
INSTANTIATE_TEST_SUITE_P(
    Programs, LirExecution,
    testing::Values(
        program_t{"int main(){unsigned long x=-1;return x/2==9223372036854775807;}", 1},
        program_t{"int main(){unsigned long x=-1;return (x>1)*10+(x>>63);}", 11},
        program_t{"int main(){unsigned int x=-1;x+=2;return x;}", 1},
        program_t{"int main(){unsigned long x=-1;return x%3;}", 0},
        program_t{"int main(){short x=-32768;unsigned short y=65535;return (x<0)*10+(y>60000);}", 11},
        program_t{"int main(){int a[2][3]={{1,2,3},{4,5,6}};int (*p)[3]=a;return (*(p+1))[2]+((p+1)-p);}", 7},
        program_t{"struct S{char a;long n;char b;};int main(){struct S a[2]={{1,20,3},{4,50,6}};struct S *p=a+1;return "
                  "p->n+p->b+(p-a);}",
                  57},
        program_t{
            "int f(int *p,int n){if(n==0)return *p;int x=n;return f(&x,n-1)+*p;}int main(){int x=7;return f(&x,3);}",
            13},
        program_t{"int g=2;int f(int *p){g+=3;*p+=4;return g;}int main(){int g=10;int old=g;return f(&g)+g+old;}", 29},
        program_t{"struct S{char a;int b;char c;};struct S f(struct S x){x.b+=2;return x;}int main(){struct S "
                  "x={1,3,5};struct S y=f(x);return x.b*10+y.b+y.c;}",
                  40},
        program_t{"struct S{int a[25];};struct S f(struct S x){x.a[24]=9;return x;}int main(){struct S x={{7}};struct "
                  "S y=f(x);return x.a[24]+y.a[0]+y.a[24];}",
                  16},
        program_t{"struct S{int x;};struct S f(struct S s){s.x++;return s;}struct S apply(struct S cb(struct S "
                  "arg),struct S s){return cb(s);}int main(){struct S s={4};return apply(f,s).x;}",
                  5},
        program_t{"int f(char a,short b,int c,long d,int e,int f,int g,int h,int i){return a+b+c+d+e+f+g+h+i;}int "
                  "main(){return f(1,2,3,4,5,6,7,8,9);}",
                  45},
        program_t{"void set(int *p){*p=6;return;}int main(){int x=2;set(&x);return x;}", 6},
        program_t{"int g;void init(){g=7;}int f(){init();return g;}int main(){return f();}", 7},
        program_t{"char *s=\"abc\";int main(){return s[2]+s[3];}", 99},
        program_t{"int main(){char a[4]=\"abc\";char *p=a+3;return *p+*(p-1);}", 99},
        program_t{"int main(){bool a[3]={2,0,-1};return a[0]*100+a[1]*10+a[2];}", 101},
        program_t{"int main(){int x=0;int a[3]={++x,++x,++x};return a[0]*100+a[1]*10+a[2];}", 123},
        program_t{"int f(int n){if(n<1)return 1;int x=n+1;return x+f(n-1)+x;}int main(){return f(4);}", 29}));
TEST(Lir, LargePartialAggregateUsesCompactDataAndCopyLoops) {
    const auto u = compile("int main(){int a[100000]={7};return a[0]+a[99999];}");
    EXPECT_LT(u.functions.front().operations.size(), 150u);
    ASSERT_EQ(u.data.size(), 1u);
    EXPECT_EQ(u.data.front().size, 400000u);
    EXPECT_EQ(u.data.front().values.size(), 1u);
    EXPECT_EQ(lir_evaluator(u).run(), 7);
}
TEST(Lir, ShortCircuitPreservesSkippedCallsAndLoops) {
    for (const bool conjunction : {false, true})
        for (int left = 0; left < 2; ++left)
            for (int right = 0; right < 2; ++right) {
                const auto source = "int g=0;int f(){g++;return " + std::to_string(right) +
                                    ";}int main(){bool x=" + std::to_string(left) + (conjunction ? "&&" : "||") +
                                    "f();return g*10+x;}";
                const auto h = high(source);
                const auto u = lir::lower(h);
                EXPECT_TRUE(lir::verify(u).empty());
                EXPECT_EQ(lir_evaluator(u).run(), hir_evaluator(h).run());
            }
}
TEST(Lir, CopiedUnitOwnsItsDataAndCfg) {
    lir::translation_unit_t copy;
    {
        const auto u = compile("int main(){int a[2]={2,3};return a[0]+a[1];}");
        copy = u;
    }
    EXPECT_TRUE(lir::verify(copy).empty());
    EXPECT_EQ(lir_evaluator(copy).run(), 5);
    const auto before = copy.functions.front().operations;
    lir::build_cfg(copy.functions.front());
    EXPECT_EQ(before, copy.functions.front().operations);
}
TEST(LirOutput, ContainsOnlyMachineOperationsAndStorageMap) {
    const auto u = compile("struct S{char c;int x;};int main(){struct S a[2]={{1,2},{3,4}};return a[1].x;}");
    std::ostringstream text, dot;
    lir::write(u, text);
    lir::write_cfg_dot(u, dot);
    for (const auto &output : {text.str(), dot.str()}) {
        EXPECT_EQ(output.find("field_address"), std::string::npos);
        EXPECT_EQ(output.find("element_address"), std::string::npos);
        EXPECT_EQ(output.find("pointer_offset"), std::string::npos);
        EXPECT_EQ(output.find("copy"), std::string::npos);
        EXPECT_NE(output.find("ld.i32"), std::string::npos);
        EXPECT_NE(output.find("st."), std::string::npos);
        EXPECT_NE(output.find("bp"), std::string::npos);
        EXPECT_NE(output.find("sp"), std::string::npos);
        EXPECT_NE(output.find(".C0"), std::string::npos);
    }
}
TEST(LirVerifier, RejectsRegistersOperandsLabelsAndCfgCorruption) {
    const auto good = compile("int main(){return 3+4;}");
    {
        auto u = good;
        u.functions[0].operations[1].operands[0] = A::reg(9999);
        EXPECT_TRUE(error_contains(u, "out of range"));
    }
    {
        auto u = good;
        u.functions[0].operations[1].operands[0] = A::immediate(0);
        EXPECT_TRUE(error_contains(u, "operand"));
    }
    {
        auto u = good;
        u.functions[0].operations.back() = {O::jump, S::u64, {A::label(9999)}};
        EXPECT_TRUE(error_contains(u, "undefined label"));
    }
    {
        auto u = good;
        u.functions[0].cfg.new_node(hir::basic_block_t{9999, 0, 1});
        EXPECT_TRUE(error_contains(u, "CFG does not match"));
    }
    {
        auto u = good;
        u.functions[0].operations[1].type = S::i32;
        EXPECT_TRUE(error_contains(u, "sp/bp"));
    }
}
TEST(LirVerifier, RejectsMissingDefinitionAndInvalidStorage) {
    {
        auto u = compile("int main(){return 3+4;}");
        const auto undefined = u.functions[0].register_count++;
        for (auto &op : u.functions[0].operations)
            if (op.opcode == O::add && op.operands[0].value >= 2) {
                op.operands[1] = A::reg(undefined);
                break;
            }
        EXPECT_TRUE(error_contains(u, "before definition"));
    }
    {
        auto u = compile("int main(){int x=1;int *p=&x;return *p;}");
        for (auto &where : u.functions[0].locations)
            if (where.kind == lir::location_kind_t::stack) where.offset = -1;
        EXPECT_TRUE(error_contains(u, "stack slot"));
    }
    {
        auto u = compile("int main(){int x=1;int y=2;&x;&y;return x+y;}");
        auto &f = u.functions[0];
        const auto slot = location(f, "x").offset;
        for (auto &where : f.locations)
            if (where.name == "y") where.offset = slot;
        EXPECT_TRUE(error_contains(u, "overlapping stack slots"));
    }
}
TEST(LirVerifier, RejectsInvalidDataAndInvalidInputHir) {
    auto u = compile("int a[2]={1};int main(){return a[0];}");
    u.data.front().values.push_back({u.data.front().size, S::u64, 1});
    EXPECT_TRUE(error_contains(u, "bounds"));
    auto h = high("int main(){return 0;}");
    h.functions[0].operations.clear();
    EXPECT_THROW(lir::lower(h), std::invalid_argument);
}
} // namespace
