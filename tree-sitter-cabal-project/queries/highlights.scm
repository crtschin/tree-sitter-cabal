; ---------- Comments ----------
(comment) @comment

; ---------- Field names ----------
(field_name) @property

; ---------- Keywords ----------
(keyword) @keyword

"if"   @keyword.conditional
"elif" @keyword.conditional
"else" @keyword.conditional

; ---------- Stanza headers ----------
(package_name) @type
(repo_name)    @module

; ---------- Literals ----------
(boolean)   @constant.builtin.boolean
(integer)   @number
(version)   @number.float
(iso_date)  @string.special
(url)       @string.special.url
(path)      @string.special.path

; ---------- Identifiers ----------
(qualified_name) @namespace
(flag_token)     @constant

; Bare identifiers in field values (enum-like values, package names in
; constraints, compiler tags such as `ghc-9.4.8`, …).
(field_value (identifier) @string)

; ---------- Predicates ----------
; Builtin predicate functions: os, arch, impl, flag
(predicate_call
  fn: (identifier) @function.builtin)

; Identifier arguments to predicate calls (linux, x86_64, ghc, foo, …)
(predicate_arg (identifier) @variable.parameter)

; Bare identifier used as a predicate atom
(predicate_or    (identifier) @variable)
(predicate_and   (identifier) @variable)
(predicate_not   (identifier) @variable)
(predicate_paren (identifier) @variable)
(if_clause   condition: (identifier) @variable)
(elif_clause condition: (identifier) @variable)

; ---------- Operators ----------
(constraint_op) @operator
"!"             @operator
"||"            @operator
"&&"            @operator

; ---------- Wildcards / globs ----------
"*" @character.special

; ---------- Punctuation ----------
"," @punctuation.delimiter
":" @punctuation.delimiter
"(" @punctuation.bracket
")" @punctuation.bracket
