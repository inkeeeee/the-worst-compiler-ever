#include "frontend/frontend.hpp"
#include "hir/hir.hpp"
#include "lir/lir.hpp"

#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    std::string input_path;
    std::string output_path;
    bool check_only = false;
    bool show_symbols = false;
    bool show_types = false;
    bool show_hir = false;
    bool show_lir = false;
    std::string cfg_path;
    std::string lir_cfg_path;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            std::cout << "Usage: frontend [--check] [--symbols] [--types] [--hir] [--lir] [--cfg CFG.dot] [--lir-cfg LIR.dot] [-o AST.dot] SOURCE.pickme\n"
                         "Semantic analysis, HIR and LIR validation run by default.\n"
                         "--check: validate without printing the AST.\n"
                         "--symbols: print scopes and bindings; --types: print every expression type.\n";
            std::cout << "--hir: print verified HIR; --cfg: write the control-flow graphs as DOT.\n";
            std::cout << "--lir: print verified LIR; --lir-cfg: write its control-flow graphs as DOT.\n";
            return 0;
        }
        if (argument == "--check") {
            check_only = true;
        } else if (argument == "--symbols") {
            show_symbols = true;
        } else if (argument == "--types") {
            show_types = true;
        } else if (argument == "--hir") {
            show_hir = true;
        } else if (argument == "--lir") {
            show_lir = true;
        } else if (argument == "--lir-cfg" && i + 1 < argc && lir_cfg_path.empty()) {
            lir_cfg_path = argv[++i];
        } else if (argument == "--cfg" && i + 1 < argc && cfg_path.empty()) {
            cfg_path = argv[++i];
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
        std::cerr << "Usage: frontend [--check] [--symbols] [--types] [--hir] [--lir] [--cfg CFG.dot] [--lir-cfg LIR.dot] [-o AST.dot] SOURCE.pickme\n";
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
        const auto ir = hir::lower(ast, semantics);
        const auto diagnostics = hir::verify(ir);
        if (!diagnostics.empty()) {
            for (const auto &diagnostic : diagnostics) {
                std::cerr << "HIR error";
                if (diagnostic.function) std::cerr << " in function #" << *diagnostic.function;
                if (diagnostic.operation) std::cerr << " at operation #" << *diagnostic.operation;
                std::cerr << ": " << diagnostic.message << '\n';
            }
            return 1;
        }
        if (show_hir) hir::write(ir, std::cout);
        const auto low = lir::lower(ir);
        const auto low_diagnostics = lir::verify(low);
        if (!low_diagnostics.empty()) {
            for (const auto &diagnostic : low_diagnostics) {
                std::cerr << "LIR error";
                if (diagnostic.function) std::cerr << " in function #" << *diagnostic.function;
                if (diagnostic.operation) std::cerr << " at operation #" << *diagnostic.operation;
                std::cerr << ": " << diagnostic.message << '\n';
            }
            return 1;
        }
        if (show_lir) lir::write(low, std::cout);
        if (!lir_cfg_path.empty()) {
            std::ofstream output(lir_cfg_path);
            if (!output) throw std::ios_base::failure("Cannot open LIR CFG output: " + lir_cfg_path);
            lir::write_cfg_dot(low, output);
            output.close();
            if (!output) throw std::ios_base::failure("Cannot save LIR CFG output: " + lir_cfg_path);
        }
        if (!cfg_path.empty()) {
            std::ofstream output(cfg_path);
            if (!output) throw std::ios_base::failure("Cannot open CFG output: " + cfg_path);
            hir::write_cfg_dot(ir, output);
            output.close();
            if (!output) throw std::ios_base::failure("Cannot save CFG output: " + cfg_path);
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
        } else if (!check_only && !show_symbols && !show_types && !show_hir && !show_lir && cfg_path.empty() && lir_cfg_path.empty()) {
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
