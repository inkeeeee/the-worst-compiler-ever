#include "lexer.hpp"

#include <utility>

namespace frontend {
lexer_t::lexer_t(std::istream &input, std::string source) : yyFlexLexer(&input), source_(std::move(source)) {}

void lexer_t::advance(std::string_view text) {
    location_.begin = location_.end;
    for (char character : text) {
        if (character == '\r') {
            ++location_.end.line;
            location_.end.column = 1;
        } else if (character == '\n') {
            if (!previous_cr_) {
                ++location_.end.line;
            }
            location_.end.column = 1;
        } else {
            ++location_.end.column;
        }
        previous_cr_ = character == '\r';
    }
}

void lexer_t::fail(std::string message, bool from_opening) const {
    auto range = location_;
    if (from_opening) {
        range.begin = opening_;
    }
    throw parse_error_t(error_kind_t::lexical, source_, range, std::move(message));
}

void lexer_t::LexerError(const char *message) {
    throw std::runtime_error(source_ + ": scanner failure: " + message);
}
}
