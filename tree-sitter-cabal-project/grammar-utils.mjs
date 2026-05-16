/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

export const PREDICATE_PRECEDENCE = {
  or: 1,
  and: 2,
  not: 3,
  call: 1,
};

// Returns rule definitions for the predicate expression grammar shared by
// both cabal and cabal-project grammars. Spread the result into the grammar's
// `rules` object.
//
// `extraArgChoices` — rule names (strings, looked up off `$`) appended to the
// `predicate_arg` choice list. cabal-project passes `["path"]`; cabal omits
// it (no `path` token; `identifier` covers realistic condition arg shapes).
export function makePredicateRules({ extraArgChoices = [] } = {}) {
  return {
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
          ...extraArgChoices.map((name) => $[name]),
          ",",
        ),
      ),
  };
}
