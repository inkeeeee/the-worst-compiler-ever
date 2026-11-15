#pragma once

#include "lir/lir.hpp"

namespace codegen {
// Emit a standalone C translation unit using runtime/vm.h. Requires int main()
// or int main(int argc, char **argv). Throws before writing on invalid input.
void write_c(const lir::translation_unit_t &unit, std::ostream &output);
} // namespace codegen
