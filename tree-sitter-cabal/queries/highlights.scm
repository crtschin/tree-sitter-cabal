; Comments
(comment) @comment

; cabal-version directive
"cabal-version" @keyword.directive
(spec_version) @number

; Field structure
(field_name)   @property
(section_type) @keyword.type
(section_name) @type

; Conditional keywords
[
  "if"
  "elseif"
  "else"
] @keyword.conditional

; Condition expression
(condition) @string.special

; Literals in field values
(boolean)         @constant.builtin.boolean
(integer)         @number
(version)         @number.float
(iso_date)        @string.special
(url)             @string.special.url
(path)            @string.special.path
(module_name)    @module
(package_name)    @string
(sublibrary_name) @string
(flag_token)      @constant
(text_fragment)   @string

; Bare identifiers in field values
(field_value (identifier) @string)

; Operators
(constraint_op) @operator
"!"             @operator
"="             @operator

; Wildcards / globs
"*" @character.special

; Punctuation
"," @punctuation.delimiter
":" @punctuation.delimiter
"(" @punctuation.bracket
")" @punctuation.bracket
"{" @punctuation.bracket
"}" @punctuation.bracket

; `<URL>`: when constraint_op nodes flank a URL they're acting as bracket
; punctuation, not as version comparison operators. The default operator
; highlight above is overridden by this more-specific pattern.
((constraint_op) @punctuation.bracket
  .
  (url)
  .
  (constraint_op) @punctuation.bracket)
