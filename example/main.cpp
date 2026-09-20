#include "graph.hpp"

#include <exception>
#include <iostream>
#include <string>

int main() {
    try {
        graph::orgraph_t<std::string, int> graph;
        auto a = graph.new_node("A");
        auto b = graph.new_node("B");
        auto c = graph.new_node("C");
        auto d = graph.new_node("D");
        auto e = graph.new_node("E");

        graph.new_edge(3, a, b);
        graph.new_edge(5, a, c);
        graph.new_edge(2, b, d);
        graph.new_edge(4, c, d);
        graph.new_edge(1, d, e);
        graph.new_edge(6, e, b);

        graph.write_dot("graph.dot",
                        [](const std::string &name) { return name; },
                        [](int weight) { return std::to_string(weight); });
        std::cout << "Graph saved to graph.dot\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
