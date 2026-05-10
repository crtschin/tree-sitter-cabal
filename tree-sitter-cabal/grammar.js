/// <reference types="tree-sitter-cli/dsl" />
// @ts-check
module.exports = grammar({
  name: "cabal",

  extras: ($) => [$.silly, $.comment, /[ \t]/],

  externals: ($) => [
    $.silly,
    $.indent,
    $.dedent,
    $._indented,
    $._newline,
  ],

  word: ($) => $.identifier,

  conflicts: ($) => [],

  rules: {
    cabal: ($) =>
      seq(
        optional($.cabal_version),
        repeat($._newline),
        $.properties,
        optional($.sections),
      ),

    cabal_version: ($) =>
      seq(repeat($._newline), "cabal-version", ":", $.spec_version),

    spec_version: ($) => /\d+\.\d+(\.\d+)?\s*\n/,

    properties: ($) => repeat1(seq($.field, repeat($._newline))),

    sections: ($) =>
      repeat1(
        seq(
          choice(
            $.benchmark,
            $.common,
            $.executable,
            $.flag,
            $.library,
            $.source_repository,
            $.test_suite,
          ),
          repeat($._newline),
        ),
      ),

    benchmark: ($) =>
      seq(
        field("type", alias("benchmark", $.section_type)),
        field("name", $.section_name),
        field("properties", $.property_block),
      ),

    common: ($) =>
      seq(
        field("type", alias("common", $.section_type)),
        field("name", $.section_name),
        field("properties", $.property_or_conditional_block),
      ),

    executable: ($) =>
      seq(
        field("type", alias("executable", $.section_type)),
        field("name", $.section_name),
        field("properties", $.property_or_conditional_block),
      ),

    flag: ($) =>
      seq(
        field("type", alias("flag", $.section_type)),
        field("name", $.section_name),
        field("properties", $.property_block),
      ),

    library: ($) =>
      seq(
        field("type", alias("library", $.section_type)),
        optional(field("name", $.section_name)),
        field("properties", $.property_or_conditional_block),
      ),

    source_repository: ($) =>
      seq(
        field("type", alias("source-repository", $.section_type)),
        field("name", $.section_name),
        field("properties", $.property_block),
      ),

    test_suite: ($) =>
      seq(
        field("type", alias("test-suite", $.section_type)),
        field("name", $.section_name),
        field("properties", $.property_or_conditional_block),
      ),

    section_name: ($) => /\d*[a-zA-Z]\w*(-\d*[a-zA-Z]\w*)*/,

    comment: ($) => token(seq("--", /.*/)),

    property_block: ($) =>
      seq(
        $.indent,
        repeat($._newline),
        repeat1(seq($.field, repeat($._newline))),
        $.dedent,
      ),

    field: ($) =>
      seq(
        $.field_name,
        ":",
        choice(
          seq(optional($.field_value), $._newline),
          seq(
            optional($.field_value),
            $.indent,
            $.field_value,
            repeat(seq(repeat1($._indented), $.field_value)),
            $.dedent,
          ),
        ),
      ),

    field_name: ($) => /\w(\w|-)+/,

    field_value: ($) => repeat1($._value_token),

    _value_token: ($) =>
      choice(
        $.boolean,
        $.iso_date,
        $.url,
        $.version,
        $.module_name,
        $.qualified_name,
        $.flag_token,
        $.integer,
        $.path,
        $.identifier,
        $.text_fragment,
        $.constraint_op,
        ",",
        "*",
        "(",
        ")",
        "!",
      ),

    boolean: ($) =>
      token(
        prec(
          7,
          choice("True", "False", "true", "false", "yes", "no", "on", "off"),
        ),
      ),

    iso_date: ($) =>
      token(
        prec(8, /[0-9]{4}-[0-9]{2}-[0-9]{2}(T[0-9]{2}:[0-9]{2}:[0-9]{2}Z)?/),
      ),

    url: ($) =>
      token(prec(9, /(https?|file|ftp|git|ssh)\+?[a-z]*:\/\/?[^\s,()]+/)),

    version: ($) => token(prec(6, /[0-9]+(\.[0-9]+)+(\.\*)?/)),

    module_name: ($) =>
      token(prec(5, /[A-Z][A-Za-z0-9_']*(\.[A-Z][A-Za-z0-9_']*)+/)),

    qualified_name: ($) =>
      token(prec(4, /[A-Za-z][A-Za-z0-9_.-]*:[A-Za-z*][A-Za-z0-9_.-]*/)),

    flag_token: ($) => token(prec(3, /[+\-][A-Za-z][A-Za-z0-9_-]*/)),

    integer: ($) => token(prec(2, /[0-9]+/)),

    identifier: ($) => token(prec(1, /[A-Za-z_][A-Za-z0-9_.\-]*/)),

    path: ($) =>
      token(
        prec(
          1,
          choice(
            /\/[A-Za-z0-9_*?.\-\/]+/,
            /\.\.?(\/[A-Za-z0-9_*?.\-\/]*)?/,
            /[A-Za-z0-9_.\-]+\/[A-Za-z0-9_*?.\-\/]*/,
          ),
        ),
      ),

    constraint_op: ($) =>
      token(choice("==", ">=", "<=", "<", ">", "^>=", "&&", "||")),

    text_fragment: ($) => token(prec(-1, /[^\s,()!*\n]+/)),

    property_or_conditional_block: ($) =>
      seq(
        $.indent,
        repeat($._newline),
        repeat1(seq(choice($.field, $.conditional), repeat($._newline))),
        $.dedent,
      ),

    conditional: ($) =>
      seq(
        $.condition_if,
        repeat($.condition_elseif),
        optional($.condition_else),
      ),

    condition_if: ($) =>
      seq("if", $.condition, $.property_or_conditional_block),
    condition_elseif: ($) =>
      seq("elseif", $.condition, $.property_or_conditional_block),
    condition_else: ($) => seq("else", $.property_or_conditional_block),
    condition: ($) => /.*/,
  },
});
