#include <stdint.h>
#include <tree_sitter/alloc.h>
#include <tree_sitter/array.h>
#include <tree_sitter/parser.h>

enum Token {
    SILLY,
    INDENT,
    DEDENT,
    INDENTED,
    NEWLINE,
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

    uint8_t cur_indent_lvl = *array_back(indent_lvls);
    // prev_indent_lvl is the level current before the most recent INDENT push.
    // A continuation line is any line more indented than prev — cabal allows
    // the hanging-comma style where later lines are less indented than the first.
    uint8_t prev_indent_lvl =
        indent_lvls->size >= 2 ? *array_get(indent_lvls, indent_lvls->size - 2) : 0;

    // Advance past the triggering '\n' and count the indent of the next
    // non-blank line.  When INDENTED is valid and the comment line is still
    // inside the value-continuation block (indent > prev_indent_lvl), we
    // consume the comment in-scanner — the token spans it, so tree-sitter will
    // not re-lex it as a (comment) node.  For all other comment positions we
    // leave the '-' characters to the normal lexer (via mark_end).
    lexer->advance(lexer, true);
    uint8_t indent = 0;
count_indent:
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
    // Cabal comments are layout-transparent: peek past any `--` comment lines
    // to find the next significant line's indent. Set mark_end before the first
    // such comment so tree-sitter re-lexes it as a `comment` extras node.
    // Normally comments at indent == cur are left for tree-sitter's extras
    // mechanism. The exception: when INDENT is the only layout token valid
    // (right after an `if`/`else` condition, before the body opens), same-level
    // comments must also be skipped so a deeper body can produce INDENT. Inside
    // an already-open block DEDENT and NEWLINE are also valid, so the exception
    // does not fire and same-level comments stay put.
    bool only_indent = valid_symbols[INDENT] && !valid_symbols[DEDENT] &&
                       !valid_symbols[NEWLINE] && !valid_symbols[INDENTED];
    bool marked = false;
    while (lexer->lookahead == '-' && (indent != cur_indent_lvl || only_indent)) {
        if (!marked) {
            lexer->mark_end(lexer);
            marked = true;
        }
        lexer->advance(lexer, true);  // past first '-'
        if (lexer->lookahead != '-') {
            break;  // single '-', not a comment; token ends before it
        }
        while (lexer->lookahead != '\n' && !lexer->eof(lexer)) {
            lexer->advance(lexer, true);
        }
        if (lexer->eof(lexer)) {
            indent = 0;
            break;
        }
        lexer->advance(lexer, true);  // past '\n'
        indent = 0;
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
    }

    if (valid_symbols[INDENT] && indent > cur_indent_lvl) {
        array_push(indent_lvls, indent);
        lexer->result_symbol = INDENT;
        return true;
    } else if (valid_symbols[INDENTED] && indent > prev_indent_lvl) {
        // Continuation line still inside the value block.
        // Accepts any indent > prev for cabal's hanging-indent style;
        // no upper bound so deeper-indented continuation lines work too.
        lexer->result_symbol = INDENTED;
        return true;
    } else if (valid_symbols[DEDENT] && indent < cur_indent_lvl) {
        // Comment-aware: if the less-indented line begins with `--`, emit
        // NEWLINE (or DEDENT) so the scanner token ends before `--` and the
        // comment is lexed as an extras (comment) node on the next pass.
        if (lexer->lookahead == '-') {
            if (!marked) {
                lexer->mark_end(lexer);
                marked = true;
            }
            lexer->advance(lexer, true);
            if (lexer->lookahead == '-') {
                if (valid_symbols[NEWLINE]) {
                    uint8_t lvl = cur_indent_lvl;
                    while (indent < lvl) {
                        array_pop(indent_lvls);
                        lvl = *array_back(indent_lvls);
                        scanner->pending_dedents++;
                    }
                    if (indent > lvl) {
                        array_push(indent_lvls, indent);
                    }
                    lexer->result_symbol = NEWLINE;
                    return true;
                } else if (valid_symbols[DEDENT]) {
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
                }
                return false;
            }
            // Single '-'. Fall through to normal DEDENT handling below.
        }
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
    } else if (valid_symbols[NEWLINE]) {
        // Fallback: line break with no indent change. Emit NEWLINE to bound
        // logical lines (e.g. between sibling fields, at top level).
        //
        // If the next non-blank line is less indented than `cur` but DEDENT
        // isn't valid yet (e.g. inside a single-line field rule that needs
        // NEWLINE first), pre-queue the dedents. Without this, the lexer has
        // already advanced past all '\n's by the time DEDENT is valid.
        if (indent < cur_indent_lvl) {
            uint8_t lvl = cur_indent_lvl;
            while (indent < lvl) {
                array_pop(indent_lvls);
                lvl = *array_back(indent_lvls);
                scanner->pending_dedents++;
            }
            if (indent > lvl) {
                array_push(indent_lvls, indent);
            }
        }
        lexer->result_symbol = NEWLINE;
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
