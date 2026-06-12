# tree-sitter-haskell-contrib

Tree-sitter grammars for Haskell-ecosystem file formats.

- **tree-sitter-cabal** — `.cabal` package description files
- **tree-sitter-cabal-project** — `cabal.project` / `*.project` workspace files
- **tree-sitter-ghc-core** — GHC Core dumps (`-ddump-simpl` output) _(scaffold; grammar in progress)_

The `.cabal` grammar was initially forked from [magus/tree-sitter-cabal](https://gitlab.com/magus/tree-sitter-cabal/).

## Setup

```sh
nix develop   # enter dev shell (provides tree-sitter, just, etc.)
```

## Commands

All commands run across every grammar via the top-level justfile.

| Command         | Description                                   |
|-----------------|-----------------------------------------------|
| `just`          | Run all tests (default)                       |
| `just test`     | Grammar unit tests + parse-corpus check       |
| `just build`    | Generate parser and build shared library      |
| `just check`    | Validate grammar without building             |
| `just fmt`      | Format grammar files (prettier + nixfmt)      |
| `just clean`    | Remove build artifacts                        |

Per-grammar commands are available as `just cabal::<cmd>`, `just cabal-project::<cmd>`, and `just ghc-core::<cmd>`.

For the cabal grammars, the `test` target also parses a corpus drawn from the
[cabal](https://github.com/haskell/cabal) and
[haskell-language-server](https://github.com/haskell/haskell-language-server)
source trees, catching regressions the inline test cases miss. The ghc-core
grammar has no external corpus yet; its `test` runs only the inline unit tests.

## References

- [Tree-sitter: Creating parsers](https://tree-sitter.github.io/tree-sitter/creating-parsers)
- [Cabal: .cabal file reference](https://cabal.readthedocs.io/en/stable/cabal-package.html)
- [Cabal: cabal.project reference](https://cabal.readthedocs.io/en/stable/cabal-project.html)
- [GHC: dumping intermediate output (`-ddump-simpl`)](https://downloads.haskell.org/ghc/latest/docs/users_guide/debugging.html)

------------------------------------------------------

Disclaimer: co-produced with a coding agent.
