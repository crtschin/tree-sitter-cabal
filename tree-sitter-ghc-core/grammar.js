/**
 * @file Tree-sitter grammar for GHC Core dumps (e.g. `-ddump-simpl` output).
 * @author Curtis Chin Jen Sem <csochinjensem@gmail.com>
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

// Models the System FC surface GHC's Core printer emits (compiler/GHC/Core/Ppr.hs,
// compiler/GHC/Iface/Type.hs). Expressions are fully brace/keyword-delimited; the
// top-level layout (each binding / type signature / Rec marker starts in column
// 0, continuations are indented) is recovered by the external scanner's
// _item_sep token (src/scanner.c), which also bounds a multi-line type signature
// from the binding line that follows it.
//
// Coverage (plan layers A-B): bindings with optional type signatures and the
// [IdInfo] bracket, Rec groups, the expression grammar (lambda, application incl.
// @type args, let/letrec/join/joinrec, jump, case + alternatives, literals,
// tuples), qualified names, a type grammar (forall, contexts, arrows incl.
// multiplicity, application, lists/tuples/kinds/promotion/equality), and
// occurrence-annotated binders. The [IdInfo] bracket is modelled coarsely as
// balanced delimiter soup (its Tmpl= template is real Core to recurse into
// later). Casts/coercions, ticks, and display modes (-dppr-debug, unicode,
// -dppr-case-as-let) are layers C-D -- see README.md.

const sepBy1 = (sep, rule) => seq(rule, repeat(seq(sep, rule)));
const sepBy = (sep, rule) => optional(sepBy1(sep, rule));

export default grammar({
  name: "ghc_core",

  externals: ($) => [$._item_sep],

  extras: ($) => [/[ \t\r\n\f]/, $.comment],

  word: ($) => $.variable,

  // After a signature's type, the next `variable` is either a type-application
  // argument or the binding name on the next line. Let GLR explore both; the
  // over-munch branch dies because the binding then can't complete.
  conflicts: ($) => [[$._type, $.type_apply]],

  rules: {
    source_file: ($) => repeat(seq($._top_item, $._item_sep)),

    _top_item: ($) => choice($.banner, $.result_size, $.rec_block, $.binding),

    // ==================== Tidy Core ====================
    banner: ($) => token(prec(1, /={4,}[^\n]*={4,}/)),

    // Result size of Tidy Core
    //   = {terms: 182, types: 90, coercions: 0, joins: 4/8}
    // "Result size of Tidy Core = {..}" -- the pass description may itself carry
    // a `{..}` record (e.g. Float out(FOS {..})), so allow one before the size.
    result_size: ($) => token(/Result size of[^{]*(\{[^{}]*\}[^{]*)?\{[^}]*\}/),

    // Rec bindings are blank-line separated (ITEM_SEP); `Rec {` abuts the first
    // and `end Rec }` abuts the last (single newlines, no ITEM_SEP).
    rec_block: ($) =>
      seq("Rec", "{", sepBy1($._item_sep, $.binding), "end", "Rec", "}"),

    // A binding, optionally preceded by its type signature (a single newline
    // away -- same binding group, no ITEM_SEP). The binders are join-point
    // parameters (empty for ordinary bindings). A multi-line signature type is
    // bounded by where the binding `name` parses (GLR), since no token separates
    // them.
    binding: ($) =>
      seq(
        optional(field("signature", $.type_signature)),
        optional(field("info", $.idinfo)),
        field("name", $.variable),
        repeat($._binder),
        "=",
        field("rhs", $._expr),
      ),

    type_signature: ($) =>
      seq($.variable, optional($.binder_annotation), "::", $._type),

    // The [IdInfo] bracket (GblId, Arity=N, Str=<..>, Cpr=.., Unf=Unf{..Tmpl=e},
    // RULES: ..). Modelled coarsely as balanced delimiter soup for now; the
    // Tmpl= template is real Core to be recursed into in a later pass.
    idinfo: ($) => prec.dynamic(1, seq("[", repeat($._soup), "]")),

    _binder: ($) =>
      choice($.variable, $.annotated_binder, $.typed_binder, $.type_binder),

    // A binder carrying an occurrence/demand annotation, e.g. x [Occ=Once1!].
    annotated_binder: ($) => seq($.variable, $.binder_annotation),

    typed_binder: ($) =>
      seq("(", $.variable, optional($.binder_annotation), "::", $._type, ")"),

    binder_annotation: ($) => prec.dynamic(1, seq("[", repeat($._soup), "]")),

    // Balanced bracket/brace/paren soup with arbitrary non-delimiter tokens.
    _soup: ($) =>
      choice(
        $._soup_token,
        seq("(", repeat($._soup), ")"),
        seq("{", repeat($._soup), "}"),
        seq("[", repeat($._soup), "]"),
      ),
    _soup_token: ($) => token(/[^\s()\[\]{}]+/),

    // Lambda-bound type variables: @a, @{a} (inferred), (@ a).
    type_binder: ($) =>
      choice(
        seq("@", $._type_atom),
        seq("@", "{", $._type, "}"),
        seq("(", "@", $._type, ")"),
      ),

    _expr: ($) =>
      choice($.lambda, $.let, $.case, $.jump, $.application, $._atom),

    _atom: ($) =>
      choice(
        $.variable,
        $.constructor,
        $.operator,
        $.literal,
        $.special_con,
        $.parens,
        $.tuple,
        $.unboxed_tuple,
      ),

    parens: ($) => seq("(", $._expr, ")"),
    tuple: ($) => seq("(", $._expr, repeat1(seq(",", $._expr)), ")"),
    unboxed_tuple: ($) => seq("(#", sepBy(",", $._expr), "#)"),

    application: ($) => prec.left(seq($._atom, repeat1($._arg))),

    _arg: ($) => choice($._atom, $.type_arg),
    type_arg: ($) => seq("@", $._type_atom),

    lambda: ($) => seq("\\", repeat1($._binder), "->", $._expr),

    jump: ($) => seq("jump", $.variable, repeat($._arg)),

    let: ($) =>
      seq(
        field("kind", choice("let", "letrec", "join", "joinrec")),
        "{",
        choice($.binding, repeat1(seq($.binding, ";"))),
        "}",
        "in",
        field("body", $._expr),
      ),

    case: ($) =>
      seq(
        "case",
        field("scrutinee", $._expr),
        "of",
        field("binder", optional(choice($.variable, $.annotated_binder))),
        "{",
        sepBy(";", $.alternative),
        "}",
      ),

    alternative: ($) =>
      seq(field("pattern", $.pattern), "->", field("rhs", $._expr)),

    pattern: ($) => choice($.literal, "__DEFAULT", $.con_pattern),

    con_pattern: ($) =>
      seq(choice($.constructor, $.special_con), repeat($._binder)),

    literal: ($) =>
      choice($._int_lit, $._float_lit, $._char_lit, $._string_lit),

    _int_lit: ($) => token(/-?[0-9]+#*/),
    _float_lit: ($) => token(/-?[0-9]+\.[0-9]+#*/),
    _char_lit: ($) => token(/'(\\.|[^'\\])'#*/),
    _string_lit: ($) => token(/"(\\.|[^"\\])*"#*/),

    // ---- types (compiler/GHC/Iface/Type.hs) ----

    _type: ($) => choice($.forall_type, $.function_type, $._type_btype),

    forall_type: ($) =>
      prec.right(
        seq(
          choice("forall", "∀"),
          repeat1($._forall_binder),
          choice(".", "->", "→"),
          $._type,
        ),
      ),
    _forall_binder: ($) => choice($.tyvar, $.kinded_tyvar),
    kinded_tyvar: ($) => seq("(", $.tyvar, "::", $._type, ")"),

    function_type: ($) => prec.right(seq($._type_btype, $._type_op, $._type)),
    _type_op: ($) =>
      choice("->", "→", "⊸", "=>", "⇒", "~", "~#", "~~", "~R#", $.mult_arrow),
    mult_arrow: ($) => seq("%", $._type_atom, choice("->", "→")),

    _type_btype: ($) => choice($.type_apply, $._type_atom),
    type_apply: ($) =>
      prec.left(seq($._type_btype, choice($._type_atom, $.kind_app))),
    kind_app: ($) => seq("@", $._type_atom),

    _type_atom: ($) =>
      choice(
        $.constructor,
        $.tyvar,
        $._type_literal,
        $.type_list,
        $.type_paren_form,
        $.unboxed_type,
        $.promoted_type,
      ),

    tyvar: ($) => $.variable,

    _type_literal: ($) => choice(token(/[0-9]+/), token(/"(\\.|[^"\\])*"/)),

    type_list: ($) => seq("[", sepBy(",", $._type), "]"),

    // Covers (), (t), (t, u, ...) and the kind signature (t :: k).
    type_paren_form: ($) =>
      seq(
        "(",
        optional(
          seq($._type, repeat(seq(",", $._type)), optional(seq("::", $._type))),
        ),
        ")",
      ),

    // (# t, ... #) unboxed tuple and (# t | ... #) unboxed sum.
    unboxed_type: ($) =>
      seq(
        "(#",
        optional(seq($._type, repeat(seq(choice(",", "|"), $._type)))),
        "#)",
      ),

    promoted_type: ($) =>
      seq(
        "'",
        choice($.constructor, $.special_con, $.type_list, $.type_paren_form),
      ),

    // ---- lexical ----

    // Optional `Module.Sub.` qualifier, then a lower/underscore/$-led name.
    variable: ($) => token(/([A-Z][A-Za-z0-9_']*\.)*[a-z_$][A-Za-z0-9_'$]*#*/),
    // Qualified upper-led data constructors / worker names (I#, GHC.Types.I#).
    constructor: ($) => token(/([A-Z][A-Za-z0-9_']*\.)*[A-Z][A-Za-z0-9_']*#*/),
    // Symbolic primops used in prefix position (+#, *#, ==#, ># ...).
    operator: ($) => token(/([A-Z][A-Za-z0-9_']*\.)*[-+*/<>=!&|^%]+#*/),
    special_con: ($) => choice("[]", ":"),

    comment: ($) => token(seq("--", /[^\n]*/)),
  },
});
