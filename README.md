# tree-sitter-cabal

Tree-sitter grammars for Haskell's Cabal build system.

- **tree-sitter-cabal** — `.cabal` package description files
- **tree-sitter-cabal-project** — `cabal.project` / `*.project` workspace files

The `.cabal` grammar was initially forked from [magus/tree-sitter-cabal](https://gitlab.com/magus/tree-sitter-cabal/).

## Setup

```sh
nix develop   # enter dev shell (provides tree-sitter, just, etc.)
```

## Commands

All commands run across both grammars via the top-level justfile.

| Command         | Description                                   |
|-----------------|-----------------------------------------------|
| `just`          | Run all tests (default)                       |
| `just test`     | Grammar unit tests + parse-corpus check       |
| `just build`    | Generate parser and build shared library      |
| `just check`    | Validate grammar without building             |
| `just fmt`      | Format grammar files (prettier + nixfmt)      |
| `just clean`    | Remove build artifacts                        |

Per-grammar commands are available as `just cabal::<cmd>` and `just cabal-project::<cmd>`.

The `test` target includes a corpus parse over the same test set from the Cabal source files (from the [cabal](https://github.com/haskell/cabal) repo) to catch regressions not covered by the inline test cases.

## References

- [Tree-sitter: Creating parsers](https://tree-sitter.github.io/tree-sitter/creating-parsers)
- [Cabal: .cabal file reference](https://cabal.readthedocs.io/en/stable/cabal-package.html)
- [Cabal: cabal.project reference](https://cabal.readthedocs.io/en/stable/cabal-project.html)
