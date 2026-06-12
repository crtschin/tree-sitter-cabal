/**
 * @file Tree-sitter grammar for GHC Core dumps (e.g. `-ddump-simpl` output).
 * @author Curtis Chin Jen Sem <csochinjensem@gmail.com>
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

// SCAFFOLD. This grammar currently models only the lexical surface of a GHC
// Core dump: phase banners, line comments, and bare whitespace-separated
// atoms. The real System FC structure (top-level bindings, type signatures,
// IdInfo brackets `[...]`, case/let/letrec, coercions, type applications) is
// still to be written. See README.md.
export default grammar({
  name: "ghc_core",

  extras: ($) => [/\s/, $.comment],

  rules: {
    source_file: ($) => repeat($._item),

    _item: ($) => choice($.banner, $.atom),

    // GHC prints a phase banner around each dump, e.g.
    //   ==================== Tidy Core ====================
    banner: ($) => token(prec(1, /={4,}[^\n]*={4,}/)),

    // Placeholder catch-all. Replace with real Core syntax rules.
    atom: ($) => token(/[^\s]+/),

    comment: ($) => token(seq("--", /[^\n]*/)),
  },
});
