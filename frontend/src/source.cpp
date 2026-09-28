#include "frontend/source.hpp"

namespace frontend {
std::ostream &operator<<(std::ostream &output, const source_range_t &range) {
    return output << range.begin.line << ':' << range.begin.column << '-' << range.end.line << ':' << range.end.column;
}

parse_error_t::parse_error_t(error_kind_t kind, std::string source, source_range_t range, std::string message)
    : std::runtime_error(source + ':' + std::to_string(range.begin.line) + ':' + std::to_string(range.begin.column) +
                         ": " + (kind == error_kind_t::lexical ? "lexical error: " : "syntax error: ") + message),
      kind_(kind), range_(range) {}
} // namespace frontend
