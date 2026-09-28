%option c++ noyywrap nounput noinput nodefault batch
%x COMMENT STRING_LITERAL CHAR_LITERAL

%{
#include "frontend/lexer.hpp"

#include <string>
#include <string_view>
#include <utility>

#undef YY_DECL
#define YY_DECL frontend::parser_t::symbol_type frontend::lexer_t::next()
#define YY_USER_ACTION advance(std::string_view(yytext, static_cast<std::size_t>(yyleng)));
#define TOKEN(name) return frontend::parser_t::make_##name(location_)
#define TEXT_TOKEN(name) return frontend::parser_t::make_##name(std::string(yytext, yyleng), location_)
%}

ID          [a-zA-Z_][a-zA-Z0-9_]*
INTEGER     [0-9]+
ESCAPE      \\([0abfnrtv\\\'\"?])

%%

[ \t\v\f]+                     {}
\r\n|\r|\n                      {}
"//"[^\r\n]*                   {}
"/*"                            { opening_ = location_.begin; BEGIN(COMMENT); }
<COMMENT>"*/"                   { BEGIN(INITIAL); }
<COMMENT>[^*\r\n]+              {}
<COMMENT>"*"                    {}
<COMMENT>\r\n|\r|\n             {}
<COMMENT><<EOF>>                { fail("unterminated block comment", true); }

\"                              { opening_ = location_.begin; literal_ = yytext; BEGIN(STRING_LITERAL); }
\'                              { opening_ = location_.begin; literal_ = yytext; character_count_ = 0; BEGIN(CHAR_LITERAL); }
<STRING_LITERAL>\"              {
                                    literal_ += yytext;
                                    location_.begin = opening_;
                                    BEGIN(INITIAL);
                                    return frontend::parser_t::make_STRING(std::move(literal_), location_);
                                }
<CHAR_LITERAL>\'                {
                                    literal_ += yytext;
                                    if (character_count_ != 1) fail("character literal must contain one character", true);
                                    location_.begin = opening_;
                                    BEGIN(INITIAL);
                                    return frontend::parser_t::make_CHARACTER(std::move(literal_), location_);
                                }
<STRING_LITERAL,CHAR_LITERAL>{ESCAPE} { literal_ += yytext; ++character_count_; }
<STRING_LITERAL>[^\\\"\r\n\x00]+  { literal_ += yytext; }
<CHAR_LITERAL>[^\\\'\r\n\x00]     { literal_ += yytext; ++character_count_; }
<STRING_LITERAL,CHAR_LITERAL>\\[^\r\n] { fail("invalid escape sequence"); }
<STRING_LITERAL,CHAR_LITERAL>\\  { fail("unfinished escape sequence"); }
<STRING_LITERAL,CHAR_LITERAL>\r\n|\r|\n { fail("newline in literal", true); }
<STRING_LITERAL,CHAR_LITERAL>\x00 { fail("NUL byte in literal"); }
<STRING_LITERAL,CHAR_LITERAL><<EOF>> { fail("unterminated literal", true); }

"void"                          { TOKEN(KW_VOID); }
"bool"                          { TOKEN(KW_BOOL); }
"char"                          { TOKEN(KW_CHAR); }
"short"                         { TOKEN(KW_SHORT); }
"int"                           { TOKEN(KW_INT); }
"long"                          { TOKEN(KW_LONG); }
"signed"                        { TOKEN(KW_SIGNED); }
"unsigned"                      { TOKEN(KW_UNSIGNED); }
"if"                            { TOKEN(KW_IF); }
"else"                          { TOKEN(KW_ELSE); }
"while"                         { TOKEN(KW_WHILE); }
"for"                           { TOKEN(KW_FOR); }
"return"                        { TOKEN(KW_RETURN); }
"break"                         { TOKEN(KW_BREAK); }
"continue"                      { TOKEN(KW_CONTINUE); }
"true"                          { TOKEN(KW_TRUE); }
"false"                         { TOKEN(KW_FALSE); }

{INTEGER}                       { TEXT_TOKEN(INTEGER); }
[0-9][a-zA-Z0-9_]*              { fail("invalid integer literal: " + std::string(yytext, yyleng)); }
{ID}                            { TEXT_TOKEN(IDENTIFIER); }

"<<="                           { TOKEN(SHL_ASSIGN); }
">>="                           { TOKEN(SHR_ASSIGN); }
"+="                            { TOKEN(ADD_ASSIGN); }
"-="                            { TOKEN(SUB_ASSIGN); }
"*="                            { TOKEN(MUL_ASSIGN); }
"/="                            { TOKEN(DIV_ASSIGN); }
"%="                            { TOKEN(MOD_ASSIGN); }
"&="                            { TOKEN(AND_ASSIGN); }
"|="                            { TOKEN(OR_ASSIGN); }
"^="                            { TOKEN(XOR_ASSIGN); }
"++"                            { TOKEN(INC); }
"--"                            { TOKEN(DEC); }
"<<"                            { TOKEN(SHL); }
">>"                            { TOKEN(SHR); }
"<="                            { TOKEN(LE); }
">="                            { TOKEN(GE); }
"=="                            { TOKEN(EQ); }
"!="                            { TOKEN(NE); }
"&&"                            { TOKEN(AND); }
"||"                            { TOKEN(OR); }
"("                             { TOKEN(LPAREN); }
")"                             { TOKEN(RPAREN); }
"{"                             { TOKEN(LBRACE); }
"}"                             { TOKEN(RBRACE); }
"["                             { TOKEN(LBRACKET); }
"]"                             { TOKEN(RBRACKET); }
";"                             { TOKEN(SEMICOLON); }
","                             { TOKEN(COMMA); }
"+"                             { TOKEN(PLUS); }
"-"                             { TOKEN(MINUS); }
"*"                             { TOKEN(STAR); }
"/"                             { TOKEN(SLASH); }
"%"                             { TOKEN(PERCENT); }
"&"                             { TOKEN(AMP); }
"|"                             { TOKEN(PIPE); }
"^"                             { TOKEN(CARET); }
"~"                             { TOKEN(TILDE); }
"!"                             { TOKEN(BANG); }
"<"                             { TOKEN(LT); }
">"                             { TOKEN(GT); }
"="                             { TOKEN(ASSIGN); }

<<EOF>>                         { location_.begin = location_.end; TOKEN(END); }
.                               { fail("unexpected byte: " + std::to_string(static_cast<unsigned char>(yytext[0]))); }

%%

int yyFlexLexer::yylex() {
    throw std::logic_error("Use frontend::lexer_t::next()");
}
