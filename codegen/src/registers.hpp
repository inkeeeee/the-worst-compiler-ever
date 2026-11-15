#pragma once

#include "lir/lir.hpp"

namespace codegen {
inline constexpr unsigned first_register = 2, register_count = 10;
inline constexpr unsigned machine_sp = 14, machine_bp = 15;

struct allocation_t {
    // Nonnegative entries are physical register numbers; negative entries are
    // -(spill slot + 1). sp/bp are mapped to 14/15, scratch registers are 0/1/12/13.
    std::vector<int> registers;
    std::vector<unsigned> saved;
    std::size_t spill_count = 0;
};

allocation_t allocate_registers(const lir::function_t &function);
bool writes_result(lir::opcode_t opcode);
} // namespace codegen
