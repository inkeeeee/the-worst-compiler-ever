%skeleton "lalr1.cc"
%require "3.8"
%define api.namespace {frontend}
%define api.parser.class {parser_t}
%define api.value.type variant
%define api.token.constructor
%define api.location.type {frontend::source_range_t}
%define parse.error detailed
%define parse.lac full
%locations
%expect 0

%code requires {
    #include "frontend/ast.hpp"
    #include <string>
    namespace frontend {
        class lexer_t;
        struct driver_t;
    }
}

%parse-param {frontend::lexer_t &lexer}
%parse-param {frontend::driver_t &driver}
%lex-param {frontend::lexer_t &lexer}

%code {
    #include "frontend/driver.hpp"
    #include "frontend/lexer.hpp"
    #include <utility>
    using K = frontend::node_kind_t;
    static frontend::parser_t::symbol_type yylex(frontend::lexer_t &lexer) {
        return lexer.next();
    }
}

%token END 0 "end of file"
%token <std::string> IDENTIFIER "identifier" INTEGER "integer" CHARACTER "character" STRING "string"
%token KW_VOID "void" KW_BOOL "bool" KW_CHAR "char" KW_SHORT "short" KW_INT "int"
%token KW_LONG "long" KW_SIGNED "signed" KW_UNSIGNED "unsigned"
%token KW_IF "if" KW_ELSE "else" KW_WHILE "while" KW_FOR "for" KW_RETURN "return"
%token KW_BREAK "break" KW_CONTINUE "continue" KW_TRUE "true" KW_FALSE "false"
%token LPAREN "(" RPAREN ")" LBRACE "{" RBRACE "}" LBRACKET "[" RBRACKET "]"
%token SEMICOLON ";" COMMA ","
%token PLUS "+" MINUS "-" STAR "*" SLASH "/" PERCENT "%"
%token AMP "&" PIPE "|" CARET "^" TILDE "~" BANG "!"
%token LT "<" LE "<=" GT ">" GE ">=" EQ "==" NE "!=" SHL "<<" SHR ">>"
%token AND "&&" OR "||" INC "++" DEC "--"
%token ASSIGN "=" ADD_ASSIGN "+=" SUB_ASSIGN "-=" MUL_ASSIGN "*=" DIV_ASSIGN "/="
%token MOD_ASSIGN "%=" AND_ASSIGN "&=" OR_ASSIGN "|=" XOR_ASSIGN "^="
%token SHL_ASSIGN "<<=" SHR_ASSIGN ">>="

%left OR
%left AND
%left PIPE
%left CARET
%left AMP
%left EQ NE
%left LT LE GT GE
%left SHL SHR
%left PLUS MINUS
%left STAR SLASH PERCENT
%precedence IF_WITHOUT_ELSE
%precedence KW_ELSE

%type <frontend::ast_node_t> translation_unit external declaration base_type declarator direct_declarator
%type <frontend::ast_node_t> init_declarators init_declarator initializer initializers parameters parameter_list parameter
%type <frontend::ast_node_t> block block_items block_item statement for_init optional_expression expression
%type <frontend::ast_node_t> assignment_expression binary_expression unary_expression
%type <frontend::ast_node_t> postfix_expression primary_expression arguments argument_list
%type <std::string> type_name signed_type integer_type assignment_operator unary_operator

%start translation_unit

%%

translation_unit:
    %empty { $$ = driver.make(K::translation_unit, "", @$); }
  | translation_unit external { $$ = driver.append($1, $2, @$); }
;

external:
    declaration { $$ = $1; }
  | base_type declarator block { $$ = driver.function($1, $2, $3, @$); }
;

base_type:
    type_name { $$ = driver.make(K::type, std::move($1), @$); }
;

type_name:
    KW_VOID { $$ = "void"; }
  | KW_BOOL { $$ = "bool"; }
  | KW_CHAR { $$ = "char"; }
  | integer_type { $$ = std::move($1); }
  | KW_SIGNED signed_type { $$ = "signed " + $2; }
  | KW_UNSIGNED signed_type { $$ = "unsigned " + $2; }
  | KW_SIGNED { $$ = "signed int"; }
  | KW_UNSIGNED { $$ = "unsigned int"; }
;

signed_type:
    integer_type { $$ = std::move($1); }
  | KW_CHAR { $$ = "char"; }
;

integer_type:
    KW_INT { $$ = "int"; }
  | KW_SHORT { $$ = "short"; }
  | KW_SHORT KW_INT { $$ = "short int"; }
  | KW_LONG { $$ = "long"; }
  | KW_LONG KW_INT { $$ = "long int"; }
  | KW_LONG KW_LONG { $$ = "long long"; }
  | KW_LONG KW_LONG KW_INT { $$ = "long long int"; }
;

declaration:
    base_type init_declarators SEMICOLON { $$ = driver.make(K::declaration, "", @$, {$1, $2}); }
;

init_declarators:
    init_declarator { $$ = driver.make(K::declarators, "", @$, {$1}); }
  | init_declarators COMMA init_declarator { $$ = driver.append($1, $3, @$); }
;

init_declarator:
    declarator { $$ = $1; }
  | declarator ASSIGN initializer { $$ = driver.make(K::initializer, "=", @$, {$1, $3}); }
;

declarator:
    direct_declarator { $$ = $1; }
  | STAR declarator { $$ = driver.make(K::pointer_declarator, "*", @$, {$2}); }
;

direct_declarator:
    IDENTIFIER { $$ = driver.make(K::name, std::move($1), @$); }
  | LPAREN declarator RPAREN { $$ = $2; $$->data().range = @$; }
  | direct_declarator LBRACKET optional_expression RBRACKET
      { $$ = driver.make(K::array_declarator, "[]", @$, {$1, $3}); }
  | direct_declarator LPAREN parameters RPAREN
      { $$ = driver.function_declarator($1, $3, @$); }
;

parameters:
    %empty { $$ = driver.make(K::parameters, "", @$); }
  | KW_VOID { $$ = driver.make(K::parameters, "void", @$); }
  | parameter_list { $$ = $1; }
;

parameter_list:
    parameter { $$ = driver.make(K::parameters, "", @$, {$1}); }
  | parameter_list COMMA parameter { $$ = driver.append($1, $3, @$); }
;

parameter:
    base_type declarator { $$ = driver.make(K::parameter, "", @$, {$1, $2}); }
;

initializer:
    assignment_expression { $$ = $1; }
  | LBRACE initializers RBRACE { $$ = $2; $$->data().range = @$; }
  | LBRACE initializers COMMA RBRACE { $$ = $2; $$->data().range = @$; }
;

initializers:
    initializer { $$ = driver.make(K::initializer_list, "", @$, {$1}); }
  | initializers COMMA initializer { $$ = driver.append($1, $3, @$); }
;

block:
    LBRACE block_items RBRACE { $$ = $2; $$->data().range = @$; }
;

block_items:
    %empty { $$ = driver.make(K::block, "", @$); }
  | block_items block_item { $$ = driver.append($1, $2, @$); }
;

block_item:
    declaration { $$ = $1; }
  | statement { $$ = $1; }
;

statement:
    block { $$ = $1; }
  | SEMICOLON { $$ = driver.make(K::empty, "", @$); }
  | expression SEMICOLON { $$ = driver.make(K::expression_statement, "", @$, {$1}); }
  | KW_IF LPAREN expression RPAREN statement %prec IF_WITHOUT_ELSE
      { $$ = driver.make(K::if_statement, "", @$, {$3, $5}); }
  | KW_IF LPAREN expression RPAREN statement KW_ELSE statement
      { $$ = driver.make(K::if_statement, "", @$, {$3, $5, $7}); }
  | KW_WHILE LPAREN expression RPAREN statement
      { $$ = driver.make(K::while_statement, "", @$, {$3, $5}); }
  | KW_FOR LPAREN for_init optional_expression SEMICOLON optional_expression RPAREN statement
      { $$ = driver.make(K::for_statement, "", @$, {$3, $4, $6, $8}); }
  | KW_RETURN optional_expression SEMICOLON { $$ = driver.make(K::return_statement, "", @$, {$2}); }
  | KW_BREAK SEMICOLON { $$ = driver.make(K::break_statement, "", @$); }
  | KW_CONTINUE SEMICOLON { $$ = driver.make(K::continue_statement, "", @$); }
;

for_init:
    declaration { $$ = $1; }
  | optional_expression SEMICOLON { $$ = $1; }
;

optional_expression:
    %empty { $$ = driver.make(K::empty, "", @$); }
  | expression { $$ = $1; }
;

expression:
    assignment_expression { $$ = $1; }
;

assignment_expression:
    binary_expression { $$ = $1; }
  | unary_expression assignment_operator assignment_expression
      { $$ = driver.make(K::assignment, std::move($2), @$, {$1, $3}); }
;

assignment_operator:
    ASSIGN { $$ = "="; }
  | ADD_ASSIGN { $$ = "+="; }
  | SUB_ASSIGN { $$ = "-="; }
  | MUL_ASSIGN { $$ = "*="; }
  | DIV_ASSIGN { $$ = "/="; }
  | MOD_ASSIGN { $$ = "%="; }
  | AND_ASSIGN { $$ = "&="; }
  | OR_ASSIGN { $$ = "|="; }
  | XOR_ASSIGN { $$ = "^="; }
  | SHL_ASSIGN { $$ = "<<="; }
  | SHR_ASSIGN { $$ = ">>="; }
;

binary_expression:
    unary_expression { $$ = $1; }
  | binary_expression PLUS binary_expression { $$ = driver.make(K::binary, "+", @$, {$1, $3}); }
  | binary_expression MINUS binary_expression { $$ = driver.make(K::binary, "-", @$, {$1, $3}); }
  | binary_expression STAR binary_expression { $$ = driver.make(K::binary, "*", @$, {$1, $3}); }
  | binary_expression SLASH binary_expression { $$ = driver.make(K::binary, "/", @$, {$1, $3}); }
  | binary_expression PERCENT binary_expression { $$ = driver.make(K::binary, "%", @$, {$1, $3}); }
  | binary_expression SHL binary_expression { $$ = driver.make(K::binary, "<<", @$, {$1, $3}); }
  | binary_expression SHR binary_expression { $$ = driver.make(K::binary, ">>", @$, {$1, $3}); }
  | binary_expression LT binary_expression { $$ = driver.make(K::binary, "<", @$, {$1, $3}); }
  | binary_expression LE binary_expression { $$ = driver.make(K::binary, "<=", @$, {$1, $3}); }
  | binary_expression GT binary_expression { $$ = driver.make(K::binary, ">", @$, {$1, $3}); }
  | binary_expression GE binary_expression { $$ = driver.make(K::binary, ">=", @$, {$1, $3}); }
  | binary_expression EQ binary_expression { $$ = driver.make(K::binary, "==", @$, {$1, $3}); }
  | binary_expression NE binary_expression { $$ = driver.make(K::binary, "!=", @$, {$1, $3}); }
  | binary_expression AMP binary_expression { $$ = driver.make(K::binary, "&", @$, {$1, $3}); }
  | binary_expression CARET binary_expression { $$ = driver.make(K::binary, "^", @$, {$1, $3}); }
  | binary_expression PIPE binary_expression { $$ = driver.make(K::binary, "|", @$, {$1, $3}); }
  | binary_expression AND binary_expression { $$ = driver.make(K::binary, "&&", @$, {$1, $3}); }
  | binary_expression OR binary_expression { $$ = driver.make(K::binary, "||", @$, {$1, $3}); }
;

unary_expression:
    postfix_expression { $$ = $1; }
  | unary_operator unary_expression { $$ = driver.make(K::unary, std::move($1), @$, {$2}); }
;

unary_operator:
    PLUS { $$ = "+"; }
  | MINUS { $$ = "-"; }
  | STAR { $$ = "*"; }
  | AMP { $$ = "&"; }
  | BANG { $$ = "!"; }
  | TILDE { $$ = "~"; }
  | INC { $$ = "++"; }
  | DEC { $$ = "--"; }
;

postfix_expression:
    primary_expression { $$ = $1; }
  | postfix_expression LBRACKET expression RBRACKET { $$ = driver.make(K::subscript, "[]", @$, {$1, $3}); }
  | postfix_expression LPAREN arguments RPAREN { $$ = driver.make(K::call, "", @$, {$1, $3}); }
  | postfix_expression INC { $$ = driver.make(K::postfix, "++", @$, {$1}); }
  | postfix_expression DEC { $$ = driver.make(K::postfix, "--", @$, {$1}); }
;

arguments:
    %empty { $$ = driver.make(K::arguments, "", @$); }
  | argument_list { $$ = $1; }
;

argument_list:
    assignment_expression { $$ = driver.make(K::arguments, "", @$, {$1}); }
  | argument_list COMMA assignment_expression { $$ = driver.append($1, $3, @$); }
;

primary_expression:
    IDENTIFIER { $$ = driver.make(K::identifier, std::move($1), @$); }
  | INTEGER { $$ = driver.make(K::integer_literal, std::move($1), @$); }
  | CHARACTER { $$ = driver.make(K::character_literal, std::move($1), @$); }
  | STRING { $$ = driver.make(K::string_literal, std::move($1), @$); }
  | KW_TRUE { $$ = driver.make(K::boolean_literal, "true", @$); }
  | KW_FALSE { $$ = driver.make(K::boolean_literal, "false", @$); }
  | LPAREN expression RPAREN { $$ = $2; $$->data().range = @$; }
;

%%

void frontend::parser_t::error(const location_type &location, const std::string &message) {
    const std::string prefix = "syntax error, ";
    const auto detail = message.starts_with(prefix) ? message.substr(prefix.size()) : message;
    throw frontend::parse_error_t(frontend::error_kind_t::syntax, driver.source, location, detail);
}
