```bash
mkdir build && cd build
cmake .. && make -j

#run tests
./tests/graph_tests        

#run example
./example/graph_example
dot -Tpng graph.got > graph.png
```
