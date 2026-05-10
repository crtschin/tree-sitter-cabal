/**
 * @file Tree sitter grammar for cabal.project files.
 * @author Curtis Chin Jen Sem <csochinjensem@gmail.com>
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const PREDICATE_PRECEDENCE = {
  or: 1,
  and: 2,
  not: 3,
  call: 1,
};

function indented_block($) {
  return optional(seq($._indent, repeat($._block_item), $._dedent));
}

export default grammar({
  name: "cabal_project",

  externals: ($) => [
    $._newline,
    $._indent,
    $._dedent,
    $._line_continuation,
    $._error_sentinel,
  ],

  extras: ($) => [/[ \t]/, $.comment],

  conflicts: ($) => [],

  word: ($) => $._word,

  rules: {
    source_file: ($) => repeat($._top_item),

    _top_item: ($) => choice($.field, $.stanza, $.conditional, $._newline),

    _block_item: ($) => choice($.field, $.conditional, $._newline),

    // ---------- Comments ----------

    comment: ($) => token(seq("--", /[^\n]*/)),

    // ---------- Fields ----------

    field: ($) =>
      seq(
        field("name", $.field_name),
        ":",
        optional(field("value", $.field_value)),
        $._newline,
      ),

    // Match any identifier-shaped name. The lexer prefers stanza-header literals
    // (`package`, `repository`, …) over this regex due to literal precedence,
    // so a stanza header isn't accidentally lexed as a field name.
    field_name: ($) => $._word,

    _word: ($) => /[A-Za-z][A-Za-z0-9_-]*/,

    // A value is any non-empty sequence of value tokens and line
    // continuations. Putting `_line_continuation` and `_value_token` in the
    // same `repeat1` lets values start on a continuation line (e.g.
    // `packages:\n    foo\n  , bar`) and span any number of indented
    // continuation lines.
    field_value: ($) => repeat1(choice($._value_token, $._line_continuation)),

    _value_token: ($) =>
      choice(
        $.boolean,
        $.iso_date,
        $.version,
        $.url,
        $.qualified_name,
        $.flag_token,
        $.integer,
        $.identifier,
        $.constraint_op,
        $.path,
        ",",
        "*",
        "(",
        ")",
        "!",
      ),

    // Tokens — declared with `token(prec(...))` for longest-match ordering.

    boolean: ($) =>
      token(
        prec(
          6,
          choice("True", "False", "true", "false", "yes", "no", "on", "off"),
        ),
      ),

    iso_date: ($) =>
      token(
        prec(7, /[0-9]{4}-[0-9]{2}-[0-9]{2}(T[0-9]{2}:[0-9]{2}:[0-9]{2}Z)?/),
      ),

    version: ($) => token(prec(5, /[0-9]+(\.[0-9]+)+(\.\*)?/)),

    url: ($) =>
      token(prec(8, /(https?|file|ftp|git|ssh)\+?[a-z]*:\/\/?[^\s,()]+/)),

    qualified_name: ($) =>
      token(prec(4, /[A-Za-z][A-Za-z0-9_.-]*:[A-Za-z*][A-Za-z0-9_.-]*/)),

    flag_token: ($) => token(prec(3, /[+\-][A-Za-z][A-Za-z0-9_-]*/)),

    integer: ($) => token(prec(2, /[0-9]+/)),

    // Identifier covers names, enum-ish values (streaming, modular),
    // versionish hyphenated tokens (ghc-9.4), and dotted/slashy path
    // fragments (setup-test/, foo/bar). Allows `/`, `.`, and `-` so that
    // path-like values lex as one token rather than getting split.
    identifier: ($) => token(prec(1, /[A-Za-z_][A-Za-z0-9_.\-\/]*/)),

    // Path tokens: bare `.` / `..`, absolute paths, relative `./` and `../`
    // paths, plus glob-y trailing `/` paths. `*` and `?` are allowed for
    // glob patterns like `/*.cabal`.
    path: ($) =>
      token(
        prec(
          1,
          choice(/\/[A-Za-z0-9_*?.\-\/]+/, /\.\.?(\/[A-Za-z0-9_*?.\-\/]*)?/),
        ),
      ),

    constraint_op: ($) =>
      token(choice("==", ">=", "<=", "<", ">", "^>=", "&&", "||")),

    // ---------- Stanzas ----------

    stanza: ($) =>
      seq(field("header", $.stanza_header), $._newline, indented_block($)),

    stanza_header: ($) =>
      choice(
        $._package_header,
        $._repository_header,
        $._source_repository_package_header,
        $._program_options_header,
        $._program_locations_header,
      ),

    _package_header: ($) =>
      seq(alias("package", $.keyword), field("name", $.package_name)),

    _repository_header: ($) =>
      seq(alias("repository", $.keyword), field("name", $.repo_name)),

    _source_repository_package_header: ($) =>
      alias("source-repository-package", $.keyword),
    _program_options_header: ($) => alias("program-options", $.keyword),
    _program_locations_header: ($) => alias("program-locations", $.keyword),

    package_name: ($) => choice("*", $._word),
    // Allow domain-style names like `packages.example.org`.
    repo_name: ($) => /[A-Za-z][A-Za-z0-9_.-]*/,

    // ---------- Conditionals ----------

    conditional: ($) =>
      seq($.if_clause, repeat($.elif_clause), optional($.else_clause)),

    if_clause: ($) =>
      seq(
        "if",
        field("condition", $._predicate_expr),
        $._newline,
        indented_block($),
      ),

    elif_clause: ($) =>
      seq(
        "elif",
        field("condition", $._predicate_expr),
        $._newline,
        indented_block($),
      ),

    else_clause: ($) => seq("else", $._newline, indented_block($)),

    _predicate_expr: ($) =>
      choice(
        $.predicate_or,
        $.predicate_and,
        $.predicate_not,
        $._predicate_atom,
      ),

    predicate_or: ($) =>
      prec.left(
        PREDICATE_PRECEDENCE.or,
        seq($._predicate_expr, "||", $._predicate_expr),
      ),

    predicate_and: ($) =>
      prec.left(
        PREDICATE_PRECEDENCE.and,
        seq($._predicate_expr, "&&", $._predicate_expr),
      ),

    predicate_not: ($) =>
      prec(PREDICATE_PRECEDENCE.not, seq("!", $._predicate_expr)),

    _predicate_atom: ($) =>
      choice($.predicate_call, $.predicate_paren, $.boolean, $.identifier),

    predicate_paren: ($) => seq("(", $._predicate_expr, ")"),

    predicate_call: ($) =>
      prec(
        PREDICATE_PRECEDENCE.call,
        seq(
          field("fn", $.identifier),
          "(",
          optional(field("arg", $.predicate_arg)),
          ")",
        ),
      ),

    predicate_arg: ($) =>
      repeat1(
        choice(
          $.boolean,
          $.version,
          $.iso_date,
          $.qualified_name,
          $.flag_token,
          $.integer,
          $.identifier,
          $.constraint_op,
          $.path,
          ",",
        ),
      ),
  },
});
