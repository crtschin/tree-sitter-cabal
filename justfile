mod cabal 'tree-sitter-cabal'
mod cabal-project 'tree-sitter-cabal-project'

default: test

# Run tests for both grammars
test: cabal::test cabal-project::test

# Build both grammars
build: cabal::build cabal-project::build

# Static checks for both grammars
check: cabal::check cabal-project::check

# Format both grammars and the flake (mode: write|check)
fmt mode="write": (cabal::fmt mode) (cabal-project::fmt mode)
    nixfmt {{ if mode == "check" { "--check" } else { "" } }} flake.nix

# Clean build artifacts in both grammars
clean: cabal::clean cabal-project::clean

# Generate flamegraphs for both grammars.
flamegraph: cabal::flamegraph cabal-project::flamegraph

# Benchmark both grammars with hyperfine.
bench: cabal::bench cabal-project::bench

# Profile both grammars under valgrind (tool = callgrind | cachegrind |
# memcheck | massif). Emits valgrind-<preset>.{out,txt} at the repo root.
valgrind tool="callgrind": (cabal::valgrind tool) (cabal-project::valgrind tool)

# Parse both corpora with scanner instrumentation enabled. Emits one
# [scanner-stats] line per grammar on stderr.
stats: cabal::stats cabal-project::stats

# Update flake inputs.
update +args:
  nix flake update {{args}}
