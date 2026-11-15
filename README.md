```
                           ___                                                                                      ___                       
           .-.            (   )                                                                               .-.  (   )                      
   .-..   ( __)   .--.     | |   ___    ___ .-. .-.     .--.       .--.      .--.    ___ .-. .-.      .-..   ( __)  | |    .--.    ___ .-.    
  /    \  (''")  /    \    | |  (   )  (   )   '   \   /    \     /    \    /    \  (   )   '   \    /    \  (''")  | |   /    \  (   )   \   
 ' .-,  ;  | |  |  .-. ;   | |  ' /     |  .-.  .-. ; |  .-. ;   |  .-. ;  |  .-. ;  |  .-.  .-. ;  ' .-,  ;  | |   | |  |  .-. ;  | ' .-. ;  
 | |  . |  | |  |  |(___)  | |,' /      | |  | |  | | |  | | |   |  |(___) | |  | |  | |  | |  | |  | |  . |  | |   | |  |  | | |  |  / (___) 
 | |  | |  | |  |  |       | .  '.      | |  | |  | | |  |/  |   |  |      | |  | |  | |  | |  | |  | |  | |  | |   | |  |  |/  |  | |        
 | |  | |  | |  |  | ___   | | `. \     | |  | |  | | |  ' _.'   |  | ___  | |  | |  | |  | |  | |  | |  | |  | |   | |  |  ' _.'  | |        
 | |  ' |  | |  |  '(   )  | |   \ \    | |  | |  | | |  .'.-.   |  '(   ) | '  | |  | |  | |  | |  | |  ' |  | |   | |  |  .'.-.  | |        
 | `-'  '  | |  '  `-' |   | |    \ .   | |  | |  | | '  `-' /   '  `-' |  '  `-' /  | |  | |  | |  | `-'  '  | |   | |  '  `-' /  | |        
 | \__.'  (___)  `.__,'   (___ ) (___) (___)(___)(___) `.__.'     `.__,'    `.__.'  (___)(___)(___) | \__.'  (___) (___)  `.__.'  (___)       
 | |                                                                                                | |                                       
(___)                                                                                              (___)                                      
```

```bash
cmake -S . -B build
cmake --build build -j
cmake --build build --target chibicc
ctest --test-dir build --output-on-failure
```

```bash
./build/frontend/frontend example/frontend.pickme
./build/frontend/frontend example/frontend.pickme --check
./build/frontend/frontend example/frontend.pickme -o ast.dot
dot -Tpng ast.dot -o ast.png
```

## Генерация C и виртуальная машина

Цепочка: исходный `.pickme` → AST → семантический анализ → HIR → LIR → C с макросами
псевдоассемблера → chibicc → исполняемый файл. Генерация C включается отдельной опцией
`--emit-c FILE`; `--emit-c -` выводит C в stdout

## Запуск генерации C

```bash
./build/frontend/frontend example/vm.pickme --emit-c build/vm.c
./third_party/chibicc/chibicc -Iruntime build/vm.c -o build/vm-demo
./build/vm-demo
```
