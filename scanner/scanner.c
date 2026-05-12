#include <stdbool.h>
#include <stdint.h>

#include <tree_sitter/alloc.h>
#include <tree_sitter/array.h>
#include <tree_sitter/parser.h>

// Layout-sensitive scanner shared by tree-sitter-cabal and tree-sitter-cabal-project.
// Cabal-syntax uses one lexer for both file formats. The .cabal vs .project distinction
// is purely semantic.
//
// ABI constraint: both grammars must list all five tokens in this exact order in their
// `externals` arrays. Tree-sitter sizes valid_symbols by the count of declared externals,
// indexed by position. Removing or reordering an entry shifts subsequent indices and
// causes out-of-bounds reads in scanner_scan.
//
// Indentation is measured in spaces. A tab in leading whitespace stops the count
// (matching Cabal's own lexer, which rejects tabs for indentation). Blank lines reset
// the count to zero and are skipped.

// NEWLINE     - End of a logical line. Fired when the next non-blank line has the same
//               or a greater indent, or when DEDENT is not yet valid and must be
//               pre-queued.
//
// INDENT      - Opens a new indented block. Pushes the new column onto the stack.
//               Only valid immediately after a block header (section name, if/elif/else).
//               The grammar never makes INDENT valid at the same time as INDENTED or
//               CONTINUATION.
//
// DEDENT      - Closes an indented block. Extra DEDENTs when multiple levels unwind at
//               once are queued in pending_dedents and drained on subsequent calls.
//
// INDENTED    - "Lenient continuation": the next line is deeper than prev_indent_lvl,
//               the level before the most recent INDENT push. The .cabal grammar uses
//               this in its multi-line field rule. After INDENT opens the value block,
//               continuation lines may sit at any column above prev, including the same
//               column as the first value line, which CONTINUATION would reject.
//
// CONTINUATION - "Strict continuation": the next line is deeper than cur_indent_lvl.
//               The .cabal-project grammar uses this so sibling fields at the same
//               column as the preceding field name are not absorbed into its value.
enum Token {
    NEWLINE,
    INDENT,
    DEDENT,
    INDENTED,
    CONTINUATION,
};

typedef struct {
    // Stack of indentation columns in spaces. Always contains at least one element:
    // the sentinel 0 at the root level.
    //   indents.back()     == cur_indent_lvl  (column of the innermost open block)
    //   indents[size - 2]  == prev_indent_lvl (column before the last INDENT push),
    //                         used by the INDENTED check; only defined when size >= 2.
    Array(uint16_t) indents;
    // DEDENTs queued for future calls. When the scanner emits NEWLINE but the next
    // line is already less indented, it pre-pops the stack and stores the deficit here.
    // Each subsequent call for DEDENT drains one count and returns without advancing.
    uint16_t pending_dedents;
} Scanner;

static void *scanner_create(void) {
    Scanner *scanner = ts_malloc(sizeof(Scanner));
    array_init(&scanner->indents);
    array_push(&scanner->indents, 0);
    scanner->pending_dedents = 0;
    return scanner;
}

static void scanner_destroy(void *payload) {
    Scanner *scanner = (Scanner *)payload;
    array_delete(&scanner->indents);
    ts_free(scanner);
}

// Advance past spaces and blank lines and return the column of the next significant
// character. Tabs stop the count (lookahead 0 at EOF also exits the loop naturally).
static uint16_t consume_blanks(TSLexer *lexer) {
    uint16_t indent = 0;
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
    return indent;
}

// Serialize scanner state to a byte buffer for tree-sitter's incremental parse cache.
// All multi-byte values are packed little-endian because the buffer has no alignment
// guarantee.
//
// Layout: [pending lo][pending hi][stack_size lo][stack_size hi]
//         [col[0] lo][col[0] hi] ... one pair per stack entry, up to the buffer limit.
//
// The 4-byte header is always written first. TREE_SITTER_SERIALIZATION_BUFFER_SIZE is
// 1024, so the header fits without a bounds check. Stack entries are guarded one by one.
static unsigned scanner_serialize(void *payload, char *buffer) {
    Scanner *scanner = (Scanner *)payload;
    unsigned size = 0;

    buffer[size++] = (char)(scanner->pending_dedents & 0xFF);
    buffer[size++] = (char)((scanner->pending_dedents >> 8) & 0xFF);

    uint16_t stack_size = (uint16_t)scanner->indents.size;
    buffer[size++] = (char)(stack_size & 0xFF);
    buffer[size++] = (char)((stack_size >> 8) & 0xFF);

    for (uint32_t i = 0; i < scanner->indents.size; i++) {
        uint16_t v = *array_get(&scanner->indents, i);
        if (size + 2 > TREE_SITTER_SERIALIZATION_BUFFER_SIZE) break;
        buffer[size++] = (char)(v & 0xFF);
        buffer[size++] = (char)((v >> 8) & 0xFF);
    }

    return size;
}

// Restore state from the buffer written by scanner_serialize.
// The guard `pos + 1 < length` ensures two bytes are available before each uint16_t
// read. If the buffer was truncated (stack too deep for 1024 bytes), we stop early and
// restore what fit. The sentinel 0 is pushed when nothing survived to preserve the
// invariant that indents is never empty.
static void scanner_deserialize(void *payload, const char *buffer, unsigned length) {
    Scanner *scanner = (Scanner *)payload;
    array_clear(&scanner->indents);
    scanner->pending_dedents = 0;

    if (length == 0) {
        array_push(&scanner->indents, 0);
        return;
    }

    unsigned pos = 0;
    uint16_t pending = (uint8_t)buffer[pos++];
    pending |= ((uint16_t)(uint8_t)buffer[pos++]) << 8;
    scanner->pending_dedents = pending;

    uint16_t stack_size = (uint8_t)buffer[pos++];
    stack_size |= ((uint16_t)(uint8_t)buffer[pos++]) << 8;

    for (uint16_t i = 0; i < stack_size && pos + 1 < length; i++) {
        uint16_t v = (uint8_t)buffer[pos++];
        v |= ((uint16_t)(uint8_t)buffer[pos++]) << 8;
        array_push(&scanner->indents, v);
    }

    if (scanner->indents.size == 0) {
        array_push(&scanner->indents, 0);
    }
}

static bool scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
    Scanner *scanner = (Scanner *)payload;

    // Drain one queued DEDENT without advancing. The lexer position was already
    // committed when the dedents were queued (consume_blanks ran in a prior call),
    // so no further advance is needed.
    if (valid_symbols[DEDENT] && scanner->pending_dedents > 0) {
        scanner->pending_dedents--;
        lexer->result_symbol = DEDENT;
        return true;
    }
    // At EOF, keep returning DEDENT for as many calls as the grammar makes. Tree-sitter
    // discards scanner state when parsing ends, so the stack is never consulted again.
    if (valid_symbols[DEDENT] && lexer->eof(lexer)) {
        lexer->result_symbol = DEDENT;
        return true;
    }
    if (lexer->eof(lexer) || lexer->lookahead != '\n') {
        return false;
    }

    uint16_t cur_indent_lvl = *array_back(&scanner->indents);
    // prev_indent_lvl: the block level before the most recent INDENT push. INDENTED
    // checks against this value, not cur, so continuation lines can sit at the same
    // column as the first value line (which pushed an INDENT above prev).
    uint16_t prev_indent_lvl =
        scanner->indents.size >= 2
            ? *array_get(&scanner->indents, scanner->indents.size - 2)
            : 0;

    // Advance past the triggering '\n' and measure the next significant line's column.
    lexer->advance(lexer, true);
    uint16_t indent = consume_blanks(lexer);

    // Cabal comments (`--` to end of line) are layout-transparent. They must not drive
    // INDENT or DEDENT decisions. We peek past any run of `--` lines to find the next
    // real line's indent, calling mark_end before the first comment so the scanner
    // token ends there. Tree-sitter then re-lexes the comment as an extras node.
    //
    // We skip comments only when they are at a different indent than cur (indent !=
    // cur_indent_lvl) or when only_indent is true. Comments at exactly cur can be left
    // to the extras mechanism under normal conditions.
    //
    // only_indent is true immediately after a block header, before the body opens.
    // In that state INDENT is the only valid layout token. A comment at the header's
    // column must be skipped so a deeper body can still produce INDENT. Once a block
    // is open, DEDENT and NEWLINE become valid too and only_indent becomes false.
    bool only_indent = valid_symbols[INDENT] && !valid_symbols[DEDENT] &&
                       !valid_symbols[NEWLINE] && !valid_symbols[INDENTED] &&
                       !valid_symbols[CONTINUATION];
    bool marked = false;
    while (lexer->lookahead == '-' && (indent != cur_indent_lvl || only_indent)) {
        if (!marked) {
            lexer->mark_end(lexer);
            marked = true;
        }
        lexer->advance(lexer, true);  // past first '-'
        if (lexer->lookahead != '-') {
            // Single '-', not a comment. mark_end already placed the token boundary
            // before this character so tree-sitter re-lexes it via the normal lexer.
            break;
        }
        while (lexer->lookahead != '\n' && !lexer->eof(lexer)) {
            lexer->advance(lexer, true);
        }
        if (lexer->eof(lexer)) {
            indent = 0;
            break;
        }
        lexer->advance(lexer, true);  // past '\n'
        indent = consume_blanks(lexer);
    }

    if (valid_symbols[INDENT] && indent > cur_indent_lvl) {
        array_push(&scanner->indents, indent);
        lexer->result_symbol = INDENT;
        return true;
    } else if (valid_symbols[CONTINUATION] && indent > cur_indent_lvl) {
        // INDENT is checked first. The grammar should not make both valid at once,
        // but this ordering makes the priority explicit.
        lexer->result_symbol = CONTINUATION;
        return true;
    } else if (valid_symbols[INDENTED] && indent > prev_indent_lvl) {
        // Deeper than prev (not cur), so a continuation line at the same column as the
        // first value line still passes.
        lexer->result_symbol = INDENTED;
        return true;
    } else if (valid_symbols[DEDENT] && indent < cur_indent_lvl) {
        // When the line that triggers DEDENT starts with `--`, place the token boundary
        // before the comment and handle the stack transition here. Tree-sitter then
        // re-lexes the `--` as a comment extras node on the next pass.
        if (lexer->lookahead == '-') {
            if (!marked) {
                lexer->mark_end(lexer);
                marked = true;
            }
            lexer->advance(lexer, true);
            if (lexer->lookahead == '-') {
                // It is a `--` comment. If NEWLINE is also valid (the grammar has not
                // yet consumed the end-of-line for the preceding construct), emit NEWLINE
                // and queue all pending DEDENTs. The grammar will request them one by one
                // via pending_dedents after the comment is re-lexed.
                if (valid_symbols[NEWLINE]) {
                    uint16_t lvl = cur_indent_lvl;
                    while (indent < lvl) {
                        array_pop(&scanner->indents);
                        lvl = *array_back(&scanner->indents);
                        scanner->pending_dedents++;
                    }
                    if (indent > lvl) {
                        array_push(&scanner->indents, indent);
                    }
                    lexer->result_symbol = NEWLINE;
                    return true;
                } else if (valid_symbols[DEDENT]) {
                    // NEWLINE is not valid. Emit one DEDENT now and queue the rest.
                    // The loop increments pending_dedents once per pop. We subtract one
                    // because we are about to emit that DEDENT directly.
                    while (indent < cur_indent_lvl) {
                        array_pop(&scanner->indents);
                        cur_indent_lvl = *array_back(&scanner->indents);
                        scanner->pending_dedents++;
                    }
                    scanner->pending_dedents--;
                    if (indent > cur_indent_lvl) {
                        array_push(&scanner->indents, indent);
                    }
                    lexer->result_symbol = DEDENT;
                    return true;
                }
                return false;
            }
            // Single '-', not a comment. The token boundary is already marked.
            // Fall through to normal DEDENT handling.
        }
        // Unwind the stack to the matching level and emit one DEDENT. Queue the rest.
        // The loop increments pending_dedents once per pop, so we subtract one for the
        // DEDENT returned here.
        //
        // If indent lands between two stack levels after unwinding (possible in error
        // recovery), push the new level so the stack stays accurate.
        while (indent < cur_indent_lvl) {
            array_pop(&scanner->indents);
            cur_indent_lvl = *array_back(&scanner->indents);
            scanner->pending_dedents++;
        }
        scanner->pending_dedents--;
        if (indent > cur_indent_lvl) {
            array_push(&scanner->indents, indent);
        }
        lexer->result_symbol = DEDENT;
        return true;
    } else if (valid_symbols[NEWLINE]) {
        // No indent change (or DEDENT not yet valid). Emit NEWLINE to close the
        // current logical line.
        //
        // Pre-queue: if the next line is already less indented than cur but the grammar
        // has not reached a state where DEDENT is valid (e.g. a single-line field rule
        // that requires NEWLINE first), unwind the stack now. By the time the grammar
        // requests DEDENT, consume_blanks has already advanced past the whitespace and
        // the indent information is gone.
        if (indent < cur_indent_lvl) {
            uint16_t lvl = cur_indent_lvl;
            while (indent < lvl) {
                array_pop(&scanner->indents);
                lvl = *array_back(&scanner->indents);
                scanner->pending_dedents++;
            }
            if (indent > lvl) {
                array_push(&scanner->indents, indent);
            }
        }
        lexer->result_symbol = NEWLINE;
        return true;
    } else {
        return false;
    }
}

// Emit both grammars' external scanner ABI symbols from this translation unit. Each
// grammar's .so links one set via its generated parser.c. The other set is dead-
// stripped by the linker or present but unreachable.
#define EXPORT(LANG)                                                                            \
    void *tree_sitter_##LANG##_external_scanner_create(void) { return scanner_create(); }       \
    void tree_sitter_##LANG##_external_scanner_destroy(void *p) { scanner_destroy(p); }         \
    unsigned tree_sitter_##LANG##_external_scanner_serialize(void *p, char *b) {                \
        return scanner_serialize(p, b);                                                         \
    }                                                                                           \
    void tree_sitter_##LANG##_external_scanner_deserialize(void *p, const char *b, unsigned l) {\
        scanner_deserialize(p, b, l);                                                           \
    }                                                                                           \
    bool tree_sitter_##LANG##_external_scanner_scan(void *p, TSLexer *l, const bool *v) {       \
        return scanner_scan(p, l, v);                                                           \
    }

EXPORT(cabal)
EXPORT(cabal_project)
