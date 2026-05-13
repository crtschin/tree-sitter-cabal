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
// Indentation is measured in spaces. Blank lines reset the count to zero and are
// skipped.
//
// Leniencies vs Cabal's own lexer (Distribution.Fields.Lexer). This scanner accepts
// input Cabal would reject; the grammar still parses such files rather than failing
// fast. Track here so the divergence is visible:
//
//   1. Tabs in indentation. Cabal rejects them. We advance to the next 8-space stop
//      (consume_blanks) and treat tabs as horizontal whitespace before a newline
//      (scanner_scan). Reason: real-world .cabal files in HLS/Cabal corpora contain
//      stray tabs; rejecting would cause spurious parse failures in editors.
//   2. NBSP (U+00A0) in indentation. Cabal treats NBSP as an ordinary character. We
//      count it as one space. Reason: paste-from-doc accidents; cheap to tolerate.
//   3. CR (\r) anywhere. Cabal rejects bare CR. We skip silently so CRLF files parse
//      identically to LF files.
//   4. Comment indent. Cabal comments (`--`) are layout-transparent regardless of
//      column. We follow that exactly; noted because it differs from Haskell's
//      layout rule, which does respect comment columns in some positions.

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
    // True once we've emitted the implicit NEWLINE at EOF. Files without a trailing
    // newline need one virtual NEWLINE so the last field/line can close, but the
    // scanner cannot advance the lexer past EOF, so firing NEWLINE unconditionally
    // would loop whenever the grammar sits in repeat($._newline). Once set, only the
    // DEDENT-at-EOF path is allowed to fire on subsequent calls.
    bool eof_newline_emitted;
} Scanner;

static void *scanner_create(void) {
    Scanner *scanner = ts_malloc(sizeof(Scanner));
    array_init(&scanner->indents);
    array_push(&scanner->indents, 0);
    scanner->pending_dedents = 0;
    scanner->eof_newline_emitted = false;
    return scanner;
}

static void scanner_destroy(void *payload) {
    Scanner *scanner = (Scanner *)payload;
    array_delete(&scanner->indents);
    ts_free(scanner);
}

// Advance past spaces and blank lines and return the column of the next significant
// character. Tabs advance to the next 8-space stop; NBSP counts as a space; \r is
// skipped silently so CRLF files behave identically to LF files. EOF (lookahead 0)
// exits the loop naturally.
static uint16_t consume_blanks(TSLexer *lexer) {
    uint16_t indent = 0;
    while (true) {
        if (lexer->lookahead == ' ' || lexer->lookahead == 0x00A0 /* nbsp */) {
            indent++;
            lexer->advance(lexer, true);
        } else if (lexer->lookahead == '\t') {
            // Advance to the next 8-space tab stop.
            indent = (uint16_t)((indent + 8) & ~(uint16_t)7);
            lexer->advance(lexer, true);
        } else if (lexer->lookahead == '\n') {
            indent = 0;
            lexer->advance(lexer, true);
        } else if (lexer->lookahead == '\r') {
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
// Layout: [pending lo][pending hi][stack_size lo][stack_size hi][eof_flag]
//         [col[0] lo][col[0] hi] ... one pair per stack entry, up to the buffer limit.
//
// The 5-byte header is always written first. TREE_SITTER_SERIALIZATION_BUFFER_SIZE is
// 1024, so the header fits without a bounds check. Stack entries are guarded one by one.
static unsigned scanner_serialize(void *payload, char *buffer) {
    Scanner *scanner = (Scanner *)payload;
    unsigned size = 0;

    buffer[size++] = (char)(scanner->pending_dedents & 0xFF);
    buffer[size++] = (char)((scanner->pending_dedents >> 8) & 0xFF);

    uint16_t stack_size = (uint16_t)scanner->indents.size;
    buffer[size++] = (char)(stack_size & 0xFF);
    buffer[size++] = (char)((stack_size >> 8) & 0xFF);

    buffer[size++] = (char)(scanner->eof_newline_emitted ? 1 : 0);

    for (uint32_t i = 0; i < scanner->indents.size; i++) {
        uint16_t v = *array_get(&scanner->indents, i);
        if (size + 2 > TREE_SITTER_SERIALIZATION_BUFFER_SIZE) break;
        buffer[size++] = (char)(v & 0xFF);
        buffer[size++] = (char)((v >> 8) & 0xFF);
    }

    return size;
}

// Restore state from the buffer written by scanner_serialize. A buffer shorter than
// the 5-byte header is treated as fresh state. The guard `pos + 1 < length` ensures
// two bytes are available before each uint16_t stack read; if the buffer was
// truncated (stack too deep for 1024 bytes), we stop early. The sentinel 0 is pushed
// when nothing survived to preserve the invariant that indents is never empty.
static void scanner_deserialize(void *payload, const char *buffer, unsigned length) {
    Scanner *scanner = (Scanner *)payload;
    array_clear(&scanner->indents);
    scanner->pending_dedents = 0;
    scanner->eof_newline_emitted = false;

    if (length < 5) {
        array_push(&scanner->indents, 0);
        return;
    }

    unsigned pos = 0;
    uint16_t pending = (uint8_t)buffer[pos++];
    pending |= ((uint16_t)(uint8_t)buffer[pos++]) << 8;
    scanner->pending_dedents = pending;

    uint16_t stack_size = (uint8_t)buffer[pos++];
    stack_size |= ((uint16_t)(uint8_t)buffer[pos++]) << 8;

    scanner->eof_newline_emitted = buffer[pos++] != 0;

    for (uint16_t i = 0; i < stack_size && pos + 1 < length; i++) {
        uint16_t v = (uint8_t)buffer[pos++];
        v |= ((uint16_t)(uint8_t)buffer[pos++]) << 8;
        array_push(&scanner->indents, v);
    }

    if (scanner->indents.size == 0) {
        array_push(&scanner->indents, 0);
    }
}

// Pop the indent stack until the top is <= indent, queuing one DEDENT per pop.
// If indent lands strictly between two stack levels (error recovery), push it so
// the stack stays accurate. No-op when indent already >= the current top.
static void unwind_to(Scanner *scanner, uint16_t indent) {
    bool popped = false;
    while (indent < *array_back(&scanner->indents)) {
        array_pop(&scanner->indents);
        scanner->pending_dedents++;
        popped = true;
    }
    if (popped && indent > *array_back(&scanner->indents)) {
        array_push(&scanner->indents, indent);
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
    // Latches once so the grammar's repeat($._newline) cannot loop on the virtual
    // NEWLINE. See eof_newline_emitted in the Scanner struct.
    if (valid_symbols[NEWLINE] && lexer->eof(lexer) &&
        !scanner->eof_newline_emitted) {
        scanner->eof_newline_emitted = true;
        lexer->result_symbol = NEWLINE;
        return true;
    }
    // Skip leading horizontal whitespace and \r so trailing spaces before a line
    // ending don't block NEWLINE/DEDENT detection. Tree-sitter calls the external
    // scanner before consuming extras, so if the lexer sits on a trailing space the
    // scanner would otherwise see ' ' as the lookahead and return false.
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
           lexer->lookahead == '\r' || lexer->lookahead == 0x00A0) {
        lexer->advance(lexer, true);
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
    // pre_block: between a block header and its yet-unopened body, GLR also reduces
    // the empty-body path so both INDENT and DEDENT are valid. In that state a
    // header-column comment must be skipped so a deeper body line behind it can still
    // produce INDENT. Inside an unclosed field only INDENT is valid, so pre_block
    // flips false and same-indent comments are left to the extras mechanism.
    bool pre_block = valid_symbols[INDENT] && valid_symbols[DEDENT];
    bool marked = false;
    while (lexer->lookahead == '-' && (indent != cur_indent_lvl || pre_block)) {
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
        // Unwind the stack and emit one DEDENT directly. The helper queues one per pop,
        // so we subtract one to account for the DEDENT returned here.
        unwind_to(scanner, indent);
        scanner->pending_dedents--;
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
        unwind_to(scanner, indent);
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
