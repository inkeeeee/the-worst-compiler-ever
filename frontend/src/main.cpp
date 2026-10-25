#include "frontend/frontend.hpp"

#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    std::string input_path;
    std::string output_path;
    bool check_only = false;
    bool show_symbols = false;
    bool show_types = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            std::cout << "Usage: frontend [--check] [--symbols] [--types] [-o AST.dot] SOURCE.pickme\n"
                         "Semantic analysis runs by default.\n"
                         "--check: validate without printing the AST.\n"
                         "--symbols: print scopes and bindings; --types: print every expression type.\n";
            return 0;
        }
        if (argument == "--check") {
            check_only = true;
        } else if (argument == "--symbols") {
            show_symbols = true;
        } else if (argument == "--types") {
            show_types = true;
        } else if (argument == "-o" && i + 1 < argc && output_path.empty()) {
            output_path = argv[++i];
        } else if (!argument.starts_with('-') && input_path.empty()) {
            input_path = argument;
        } else {
            std::cerr << "Invalid argument: " << argument << '\n';
            return 2;
        }
    }
    if (input_path.empty()) {
        std::cerr << "Usage: frontend [--check] [--symbols] [--types] [-o AST.dot] SOURCE.pickme\n";
        return 2;
    }
    try {
        const auto ast = frontend::parse_file(input_path);
        const auto semantics = frontend::analyze(ast, input_path);
        if (show_symbols) semantics.write_symbols(std::cout);
        if (show_types) semantics.write_types(std::cout);
        if (!semantics.ok()) {
            semantics.write_diagnostics(std::cerr);
            return 1;
        }
        if (!output_path.empty()) {
            std::ofstream output(output_path);
            if (!output) {
                throw std::ios_base::failure("Cannot open output: " + output_path);
            }
            ast.write_dot(output);
            output.close();
            if (!output) {
                throw std::ios_base::failure("Cannot save output: " + output_path);
            }
        } else if (!check_only && !show_symbols && !show_types) {
            ast.dump(std::cout);
        }
    } catch (const frontend::parse_error_t &error) {
        std::cerr << error.what() << '\n';
        return 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
    return 0;
}
