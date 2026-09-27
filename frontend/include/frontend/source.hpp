#pragma once

#include <cstddef>
#include <ostream>
#include <stdexcept>
#include <string>

namespace frontend {
struct position_t {
    std::size_t line = 1;
    std::size_t column = 1;
};

struct source_range_t {
    position_t begin;
    position_t end;
};

std::ostream &operator<<(std::ostream &output, const source_range_t &range);

enum class error_kind_t { lexical, syntax };

class parse_error_t : public std::runtime_error {
  public:
    parse_error_t(error_kind_t kind, std::string source, source_range_t range, std::string message);

    error_kind_t kind() const noexcept { return kind_; }
    const source_range_t &range() const noexcept { return range_; }

  private:
    error_kind_t kind_;
    source_range_t range_;
};
}
