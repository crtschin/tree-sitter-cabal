#include "tree_sitter/parser.h"
#include "tree_sitter/array.h"

#include <stdint.h>
#include <stdbool.h>

enum TokenType {
  NEWLINE,
  INDENT,
  DEDENT,
  LINE_CONTINUATION,
  ERROR_SENTINEL,
};

typedef struct {
  Array(uint16_t) indents;
  uint16_t pending_dedents;
  bool eof_newline_emitted;
  // True when the lexer position is at the start of a new logical line
  // (after we consumed a `\n` and possibly some leading whitespace). We use
  // this to decide whether to emit INDENT/DEDENT based on the current
  // column.
  bool at_line_start;
} Scanner;

void *tree_sitter_cabal_project_external_scanner_create(void) {
  Scanner *scanner = ts_calloc(1, sizeof(Scanner));
  array_init(&scanner->indents);
  array_push(&scanner->indents, 0);
  scanner->pending_dedents = 0;
  scanner->eof_newline_emitted = false;
  scanner->at_line_start = true;
  return scanner;
}

void tree_sitter_cabal_project_external_scanner_destroy(void *payload) {
  Scanner *scanner = (Scanner *)payload;
  array_delete(&scanner->indents);
  ts_free(scanner);
}

unsigned tree_sitter_cabal_project_external_scanner_serialize(void *payload, char *buffer) {
  Scanner *scanner = (Scanner *)payload;
  unsigned size = 0;

  buffer[size++] = (char)(scanner->pending_dedents & 0xFF);
  buffer[size++] = (char)((scanner->pending_dedents >> 8) & 0xFF);
  buffer[size++] = (char)(scanner->eof_newline_emitted ? 1 : 0);
  buffer[size++] = (char)(scanner->at_line_start ? 1 : 0);

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

void tree_sitter_cabal_project_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  Scanner *scanner = (Scanner *)payload;
  array_clear(&scanner->indents);
  scanner->pending_dedents = 0;
  scanner->eof_newline_emitted = false;
  scanner->at_line_start = true;

  if (length == 0) {
    array_push(&scanner->indents, 0);
    return;
  }

  unsigned pos = 0;
  uint16_t pending = (uint8_t)buffer[pos++];
  pending |= ((uint16_t)(uint8_t)buffer[pos++]) << 8;
  scanner->pending_dedents = pending;

  scanner->eof_newline_emitted = buffer[pos++] != 0;
  scanner->at_line_start = buffer[pos++] != 0;

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

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }
static inline void skip(TSLexer *lexer)    { lexer->advance(lexer, true);  }

static bool handle_eof(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
  if (scanner->indents.size > 1 && valid_symbols[DEDENT]) {
    uint16_t pops = (uint16_t)(scanner->indents.size - 1);
    scanner->pending_dedents = (uint16_t)(pops - 1);
    (void)array_pop(&scanner->indents);
    lexer->result_symbol = DEDENT;
    return true;
  }
  if (!scanner->eof_newline_emitted && valid_symbols[NEWLINE]) {
    scanner->eof_newline_emitted = true;
    lexer->result_symbol = NEWLINE;
    return true;
  }
  return false;
}

bool tree_sitter_cabal_project_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  Scanner *scanner = (Scanner *)payload;

  // Drain any queued DEDENTs first.
  if (scanner->pending_dedents > 0 && valid_symbols[DEDENT]) {
    scanner->pending_dedents--;
    if (scanner->indents.size > 1) {
      (void)array_pop(&scanner->indents);
    }
    lexer->result_symbol = DEDENT;
    return true;
  }

  // Skip leading horizontal whitespace at the current position. After this
  // loop, the lexer's column equals the indent depth IF we are at the start
  // of a new line.
  while (lexer->lookahead == ' ' || lexer->lookahead == '\t' || lexer->lookahead == '\r') {
    skip(lexer);
  }

  // EOF: drain the stack with DEDENTs, then emit one terminal NEWLINE.
  if (lexer->eof(lexer)) {
    return handle_eof(scanner, lexer, valid_symbols);
  }

  // If we're at the start of a new line and an INDENT or DEDENT is needed,
  // compare the current column (which equals the indent depth after the
  // whitespace skip above) to the indent stack.
  if (scanner->at_line_start && lexer->lookahead != '\n' &&
      (valid_symbols[INDENT] || valid_symbols[DEDENT] || valid_symbols[LINE_CONTINUATION])) {
    uint32_t col = lexer->get_column(lexer);
    uint16_t current = *array_back(&scanner->indents);

    if (col > current) {
      if (valid_symbols[INDENT]) {
        array_push(&scanner->indents, (uint16_t)col);
        scanner->at_line_start = false;
        lexer->result_symbol = INDENT;
        return true;
      }
      if (valid_symbols[LINE_CONTINUATION]) {
        scanner->at_line_start = false;
        lexer->result_symbol = LINE_CONTINUATION;
        return true;
      }
      // Fall through: parser doesn't want INDENT here, just continue.
    } else if (col < current) {
      // Before emitting a DEDENT, check if the current line is a `--`
      // comment. If so, return false — the internal lexer will consume the
      // comment as an `extra` token, and the next scanner call will
      // re-evaluate the indent after the comment's trailing newline.
      // We can advance speculatively here: if we return false, tree-sitter
      // resets the lexer to the position before this scanner call.
      if (lexer->lookahead == '-') {
        advance(lexer);
        if (lexer->lookahead == '-') {
          return false;
        }
        // Single `-` (flag/operator): fall through to DEDENT below.
      }
      if (valid_symbols[DEDENT]) {
        uint32_t i = scanner->indents.size;
        uint16_t pops = 0;
        while (i > 1 && *array_get(&scanner->indents, i - 1) > col) {
          pops++;
          i--;
        }
        if (pops > 0) {
          scanner->pending_dedents = (uint16_t)(pops - 1);
          (void)array_pop(&scanner->indents);
          // Stay at_line_start = true so the next call can keep popping or
          // continue to the column == current branch.
          lexer->result_symbol = DEDENT;
          return true;
        }
      }
    }
    // col == current: fall through. The parser already received a NEWLINE
    // for this line's start; nothing more to emit from us.
    scanner->at_line_start = false;
  }

  if (lexer->lookahead != '\n') {
    return false;
  }

  // Consume the line break and any subsequent blank lines so the parser
  // sees a single logical line break, not many. (Comment-only lines aren't
  // detected here — we'd need 2-char lookahead. Comments at the same indent
  // as surrounding code parse fine via the grammar's `extras`.)
  advance(lexer);
  for (;;) {
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t' || lexer->lookahead == '\r') {
      skip(lexer);
    }
    if (lexer->lookahead == '\n') {
      advance(lexer);
      continue;
    }
    break;
  }
  lexer->mark_end(lexer);

  // After consuming the newline, emit NEWLINE. The next call will (via
  // at_line_start) compare the current column and emit INDENT/DEDENT if
  // needed.
  scanner->at_line_start = true;

  if (lexer->eof(lexer)) {
    return handle_eof(scanner, lexer, valid_symbols);
  }

  // Decide which token to emit based on the next line's indent. When the
  // next line is more-indented than the current block, prefer
  // LINE_CONTINUATION so that field values can span continuation lines.
  uint32_t col = lexer->get_column(lexer);
  uint16_t current = *array_back(&scanner->indents);

  if (col > current) {
    if (valid_symbols[LINE_CONTINUATION]) {
      scanner->at_line_start = false;
      lexer->result_symbol = LINE_CONTINUATION;
      return true;
    }
    if (valid_symbols[NEWLINE]) {
      // INDENT will be emitted on the next call via the at_line_start path.
      lexer->result_symbol = NEWLINE;
      return true;
    }
    if (valid_symbols[INDENT]) {
      array_push(&scanner->indents, (uint16_t)col);
      scanner->at_line_start = false;
      lexer->result_symbol = INDENT;
      return true;
    }
    return false;
  }

  // col == current OR col < current: emit NEWLINE; subsequent calls will
  // emit any required DEDENTs via the at_line_start path.
  if (valid_symbols[NEWLINE]) {
    lexer->result_symbol = NEWLINE;
    return true;
  }

  return false;
}
