#pragma once

#ifndef yyFlexLexerOnce
#include <FlexLexer.h>
#endif

#include "parser.hpp"

#include <istream>
#include <string>
#include <string_view>

namespace frontend {
class lexer_t : public yyFlexLexer {
  public:
    lexer_t(std::istream &input, std::string source);
    parser_t::symbol_type next();

  private:
    std::string source_;
    source_range_t location_;
    position_t opening_;
    std::string literal_;
    std::size_t character_count_ = 0;
    bool previous_cr_ = false;

    void advance(std::string_view text);
    [[noreturn]] void fail(std::string message, bool from_opening = false) const;
    void LexerError(const char *message) override;
};
}
