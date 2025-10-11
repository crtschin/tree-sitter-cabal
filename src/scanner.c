#include <stdint.h>
#include <tree_sitter/alloc.h>
#include <tree_sitter/array.h>
#include <tree_sitter/parser.h>

enum Token {
    SILLY,
    INDENT,
    DEDENT,
    INDENTED,
};

typedef struct Scanner Scanner;
struct Scanner {
    void *indent_lvls;
    uint8_t pending_dedents;
};

static unsigned
scanner_serialize(Scanner *scanner, char *buffer)
{
    Array(uint8_t) *indent_lvls = scanner->indent_lvls;
    size_t i = 0;
    buffer[i++] = scanner->pending_dedents;
    if (indent_lvls->size > 0) {
        array_erase(indent_lvls, 0);
    }
    for (size_t n = 0; n < indent_lvls->size; n++) {
        buffer[i++] = *array_get(indent_lvls, n);
    }
    return indent_lvls->size + 1;
}

static void
scanner_deserialize(Scanner *scanner, char const *buffer, unsigned length)
{
    Array(uint8_t) *indent_lvls = scanner->indent_lvls;
    scanner->pending_dedents = 0;
    array_clear(indent_lvls);
    array_push(indent_lvls, 0);

    if (length > 0) {
        size_t i = 0;
        scanner->pending_dedents = buffer[i++];
        for (; i < length; i++) {
            array_push(indent_lvls, buffer[i]);
        }
    }
}

static bool
scanner_scan(Scanner *scanner, TSLexer *lexer, bool const *valid_symbols)
{
    Array(uint8_t) *indent_lvls = scanner->indent_lvls;

    if (valid_symbols[DEDENT] && scanner->pending_dedents > 0) {
        scanner->pending_dedents--;
        lexer->result_symbol = DEDENT;
        return true;
    } else if (valid_symbols[DEDENT] && lexer->eof(lexer)) {
        lexer->result_symbol = DEDENT;
        return true;
    } else if (lexer->eof(lexer)) {
        return false;
    } else if (lexer->lookahead != '\n') {
        return false;
    }

    lexer->advance(lexer, true);
    uint8_t indent = 0;
    while (true) {
        if (lexer->lookahead == ' ') {
            indent++;
            lexer->advance(lexer, true);
        } else if (lexer->lookahead == '\n') {
            indent = 0;
            lexer->advance(lexer, true);
        } else {
            break;
        }
    }

    uint8_t cur_indent_lvl = *array_back(indent_lvls);
    if (valid_symbols[INDENT] && indent > cur_indent_lvl) {
        array_push(indent_lvls, indent);
        lexer->result_symbol = INDENT;
        return true;
    } else if (valid_symbols[DEDENT] && indent < cur_indent_lvl) {
        while (indent < cur_indent_lvl) {
            array_pop(indent_lvls);
            cur_indent_lvl = *array_back(indent_lvls);
            scanner->pending_dedents++;
        }
        scanner->pending_dedents--;

        if (indent > cur_indent_lvl) {
            array_push(indent_lvls, indent);
        }

        lexer->result_symbol = DEDENT;
        return true;
    } else if (valid_symbols[INDENTED] && indent > 0) {
        lexer->result_symbol = INDENTED;
        return true;
    } else {
        return false;
    }
}

void *
tree_sitter_cabal_external_scanner_create()
{
    Scanner *scanner = ts_malloc(sizeof(Scanner));
    scanner->pending_dedents = 0;
    Array(uint8_t) *indent_lvls = ts_malloc(sizeof(Array(uint8_t)));
    array_init(indent_lvls);
    scanner->indent_lvls = indent_lvls;
    return scanner;
}

bool
tree_sitter_cabal_external_scanner_scan(void *payload,
                                        TSLexer *lexer,
                                        bool const *valid_symbols)
{
    Scanner *scanner = payload;
    return scanner_scan(scanner, lexer, valid_symbols);
}

unsigned
tree_sitter_cabal_external_scanner_serialize(void *payload, char *buffer)
{
    Scanner *scanner = payload;
    return scanner_serialize(scanner, buffer);
}

void
tree_sitter_cabal_external_scanner_deserialize(void *payload,
                                               char const *buffer,
                                               unsigned length)
{
    Scanner *scanner = payload;
    scanner_deserialize(scanner, buffer, length);
}

void
tree_sitter_cabal_external_scanner_destroy(void *payload)
{
    Scanner *scanner = payload;
    ts_free(scanner->indent_lvls);
    ts_free(scanner);
}
