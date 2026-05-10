; Comments
(comment) @comment

; cabal-version directive
"cabal-version" @keyword.directive
(spec_version) @number

; Fields
(field_name) @property
(field_value) @string

; Sections
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

; Punctuation
":" @punctuation.delimiter
