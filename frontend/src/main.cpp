#include "frontend/frontend.hpp"

#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    std::string input_path;
    std::string output_path;
    bool check_only = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            std::cout << "Usage: frontend [--check] [-o AST.dot] SOURCE.pickme\n"
                         "Use - as SOURCE to read stdin. Without --check or -o, print the AST.\n";
            return 0;
        }
        if (argument == "--check") {
            check_only = true;
        } else if (argument == "-o" && i + 1 < argc && output_path.empty()) {
            output_path = argv[++i];
        } else if ((argument == "-" || !argument.starts_with('-')) && input_path.empty()) {
            input_path = argument;
        } else {
            std::cerr << "Invalid argument: " << argument << '\n';
            return 2;
        }
    }
    if (input_path.empty()) {
        std::cerr << "Usage: frontend [--check] [-o AST.dot] SOURCE.pickme\n";
        return 2;
    }
    try {
        const auto ast = input_path == "-" ? frontend::parse(std::cin, "<stdin>") : frontend::parse_file(input_path);
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
        } else if (!check_only) {
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
