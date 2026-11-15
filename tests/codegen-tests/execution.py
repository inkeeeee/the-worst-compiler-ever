#!/usr/bin/env python3
"""Compile .pickme -> macro C -> chibicc executable, then check real execution."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


parser = argparse.ArgumentParser()
parser.add_argument("--frontend", type=Path, required=True)
parser.add_argument("--cc", type=Path, required=True)
parser.add_argument("--runtime", type=Path, required=True)
parser.add_argument("--examples", type=Path, required=True)
OPTIONS, remaining = parser.parse_known_args()


class Execution(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="pickme-codegen-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def command(self, argv):
        result = subprocess.run([str(a) for a in argv], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def compile(self, source):
        source_path = self.root / "input.pickme"
        c_path = self.root / "output.c"
        binary = self.root / "program"
        source_path.write_text(source)
        self.command([OPTIONS.frontend, source_path, "--emit-c", c_path])
        self.command([OPTIONS.cc, "-I" + str(OPTIONS.runtime), c_path, "-o", binary])
        code = c_path.read_text()
        # Outside global declarations, generated functions have no C statements
        # except individual VM operations, and never acquire C parameters/locals.
        in_function = False
        for line in code.splitlines():
            if re.fullmatch(r"static void \w+\(void\) \{", line):
                in_function = True
            elif line == "}":
                in_function = False
            elif in_function:
                self.assertRegex(line.strip(), r"^VM_[A-Z_]+\(.*\);?$")
        self.assertNotIn("r[", code)
        return binary, code

    def execute(self, source, expected=0, stdout="", args=()):
        binary, code = self.compile(source)
        result = subprocess.run([str(binary), *map(str, args)], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, expected & 255, result.stderr)
        self.assertEqual(result.stdout, stdout)
        return code

    def test_arithmetic_widths_and_comparisons(self):
        cases = [
            ("unsigned long x=-1;return x/2==9223372036854775807;", 1),
            ("unsigned long x=-1;return (x>1)*10+(x>>63);", 11),
            ("unsigned int x=-1;x+=2;return x;", 1),
            ("unsigned long x=-1;return x%3;", 0),
            ("short x=-32768;unsigned short y=65535;return (x<0)*10+(y>60000);", 11),
            ("int x=-19;return (x/4==-4)*10+(x%4==-3);", 11),
            ("int x=-16;return (x>>2)==-4;", 1),
            ("long x=-9223372036854775807-1;return (-x==x)&&(x+(-1)==9223372036854775807);", 1),
            ("unsigned int x=4294967295;return ((x<<1)==4294967294)&&((x*3)==4294967293);", 1),
            ("signed char x=255;unsigned char y=255;return (x==-1)*10+(y==255);", 11),
            ("bool a=6;bool b=0;return a*10+b;", 10),
            ("int x=12;return ((x&10)==8)&&((x|3)==15)&&((x^10)==6)&&(~x==-13);", 1),
            ("int x=2;return (x<3)+(x<=2)+(x>1)+(x>=2)+(x==2)+(x!=3);", 6),
        ]
        for body, expected in cases:
            with self.subTest(body=body):
                self.execute("int main(){" + body + "}", expected)

    def test_arrays_structs_and_pointers(self):
        cases = [
            ("int main(){int a[2][3]={{1,2,3},{4,5,6}};int (*p)[3]=a;return (*(p+1))[2]+((p+1)-p);}", 7),
            ("struct S{char a;long n;char b;};int main(){struct S a[2]={{1,20,3},{4,50,6}};"
             "struct S *p=a+1;return p->n+p->b+(p-a);}", 57),
            ("struct S{char a;int b;char c;};struct S f(struct S x){x.b+=2;return x;}"
             "int main(){struct S x={1,3,5};struct S y=f(x);return x.b*10+y.b+y.c;}", 40),
            ("struct S{int a[25];};struct S f(struct S x){x.a[24]=9;return x;}"
             "int main(){struct S x={{7}};struct S y=f(x);return x.a[24]+y.a[0]+y.a[24];}", 16),
            ("struct S{int x;};struct S f(struct S s){s.x++;return s;}"
             "struct S apply(struct S cb(struct S arg),struct S s){return cb(s);}"
             "int main(){struct S s={4};return apply(f,s).x;}", 5),
            ("int main(){int x=0;int a[3]={++x,++x,++x};return a[0]*100+a[1]*10+a[2];}", 123),
            ("int main(){int a[100000]={7};return a[0]+a[99999];}", 7),
            ("char *s=\"abc\";int main(){return s[2]+s[3];}", 99),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.execute(source, expected)

    def test_recursive_and_indirect_calls(self):
        self.execute("int f(int n){if(n<1)return 1;int x=n+1;return x+f(n-1)+x;}int main(){return f(4);}", 29)
        self.execute("int f(int *p,int n){if(n==0)return *p;int x=n;return f(&x,n-1)+*p;}"
                     "int main(){int x=7;return f(&x,3);}", 13)
        self.execute("int add(int x,int y){return x+y;}int sub(int x,int y){return x-y;}"
                     "int main(){int (*f)(int a,int b)=add;int x=f(9,3);f=sub;return x+f(9,3);}", 18)
        self.execute("int f(char a,short b,int c,long d,int e,int f,int g,int h,int i){return a+b+c+d+e+f+g+h+i;}"
                     "int main(){return f(1,2,3,4,5,6,7,8,9);}", 45)
        self.execute("int odd(int x);int even(int x){if(x==0)return 1;return odd(x-1);}"
                     "int odd(int x){if(x==0)return 0;return even(x-1);}int main(){return even(20);}", 1)
        self.execute("int f(int x){return x+1;}int g(int x){return x+2;}"
                     "int (*table[2])(int x)={f,g};int main(){return table[0](3)+table[1](4);}", 10)
        self.execute("int f(int x){return x+1;}int g(int x){return x+2;}"
                     "int (*choose(int flag))(int arg){if(flag)return f;return g;}"
                     "int main(){return choose(0)(4)+choose(1)(3);}", 10)

    def test_spills_across_loops_branches_and_recursion(self):
        declarations = "".join(f"int x{i}=n+{i};" for i in range(18))
        total = "+".join(f"x{i}" for i in range(18))
        source = ("int f(int n){" + declarations + "int y=0;if(n>0)y=f(n-1);"
                  "for(int k=0;k<4;k++){if(k%2==0)x0+=k;else x17+=k;}return y+" + total + ";}"
                  "int main(){return f(2)==531;}")
        code = self.execute(source, 1)
        self.assertTrue(any(int(n) > 0 for n in re.findall(r"spills=(\d+)", code)))

    def test_loops_and_short_circuit(self):
        self.execute("int main(){int sum=0;for(int i=0;i<10;i++){if(i==2)continue;if(i==8)break;sum+=i;}"
                     "while(sum>20)sum--;return sum;}", 20)
        for op in ("&&", "||"):
            for left in (0, 1):
                for right in (0, 1):
                    called = left if op == "&&" else 1 - left
                    value = (left and right) if op == "&&" else (left or right)
                    self.execute(f"int g=0;int f(){{g++;return {right};}}"
                                 f"int main(){{bool x={left}{op}f();return g*10+x;}}", called * 10 + value)

    def test_argc_argv_survive_global_initializers(self):
        self.execute("int f(){return 8;}int g=f();int h=g+1;"
                     "int main(int argc,char **argv){return argc*10+g+h+argv[1][0]-'a';}", 47, args=["abc", "def"])

    def test_malloc_free_and_function_addresses(self):
        self.execute("void *malloc(unsigned long size);void free(void *p);"
                     "int main(){void *(*alloc)(unsigned long size)=malloc;void (*release)(void *p)=free;"
                     "int *a=alloc(12);a[0]=7;a[1]=11;a[2]=13;int result=a[0]+a[1]+a[2];release(a);return result;}", 31)

    def test_all_io_wrappers(self):
        path = self.root / "io.txt"
        self.execute("int open(char *path,int flags,int mode);int close(int fd);"
                     "long write(int fd,void *buf,unsigned long n);long read(int fd,void *buf,unsigned long n);"
                     "int main(int argc,char **argv){char buf[4]={0};int fd=open(argv[1],577,384);"
                     "if(fd<0)return 1;if(write(fd,\"abc\",3)!=3)return 2;if(close(fd)!=0)return 3;"
                     "fd=open(argv[1],0,0);if(read(fd,buf,3)!=3)return 4;if(close(fd)!=0)return 5;"
                     "write(1,buf,3);return buf[3];}", stdout="abc", args=[path])
        self.assertEqual(path.read_bytes(), b"abc")

    def test_open_two_arguments_and_errors(self):
        self.execute("int open(char *p,int flags);int close(int fd);long read(int fd,void *buf,unsigned long n);"
                     "int main(int argc,char **argv){char c;return (open(argv[1],0)==-1)"
                     "+(close(-1)==-1)+(read(-1,&c,1)==-1);}", 3, args=[self.root / "missing"])

    def test_user_definition_overrides_host_adapter(self):
        self.execute("int close(int fd){return fd+3;}int main(){return close(4);}", 7)

    def test_examples(self):
        self.execute((OPTIONS.examples / "hir.pickme").read_text(), 11)
        self.execute((OPTIONS.examples / "vm.pickme").read_text(), stdout="OK\n")

    def test_rejects_bad_entry_or_external_without_truncating_output(self):
        cases = [
            ("int f(){return 0;}", "no main"),
            ("int main(int x){return x;}", "main must be"),
            ("int mystery(int x);int main(){return mystery(1);}", "unsupported external"),
            ("int malloc(int n);int main(){return malloc(8);}", "incompatible declaration"),
        ]
        for source, message in cases:
            with self.subTest(source=source):
                path = self.root / "input.pickme"
                output = self.root / "output.c"
                path.write_text(source)
                output.write_text("keep this")
                result = subprocess.run([str(OPTIONS.frontend), str(path), "--emit-c", str(output)],
                                        capture_output=True, text=True, timeout=10)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)
                self.assertEqual(output.read_text(), "keep this")

    def test_runtime_unaligned_memory_and_entry_registers(self):
        path = self.root / "runtime.c"
        binary = self.root / "runtime"
        path.write_text('''#include "vm.h"
VM_DEFINE_MACHINE();
static unsigned char memory[16];
static const uint64_t value = UINT64_C(9223372036854775811);
static void entry(void) {
    VM_ADDR(3, memory);
    VM_ADDR(4, value);
    VM_LD(uint64_t, 4, 4, 0);
    VM_ST(uint64_t, 3, 4, 1);
    VM_LD(uint64_t, 5, 3, 1);
    VM_NE(uint64_t, 0, 4, 5);
    VM_HALT();
}
VM_MAIN(entry)
''')
        self.command([OPTIONS.cc, "-I" + str(OPTIONS.runtime), path, "-o", binary])
        self.command([binary])


if __name__ == "__main__":
    if not OPTIONS.cc.is_file():
        parser.error("chibicc not found; run: cmake --build build --target chibicc")
    unittest.main(argv=[__file__, *remaining])
