#!/usr/bin/env bash
# Regenerate the committed Core/STG/Cmm dump fixtures for one grammar from the
# nixpkgs-pinned GHC: compile the shared Haskell sources in test/fixtures/ and
# capture the dump at the requested pipeline stage, under flag sets the
# harvested corpus under-represents (-dsuppress extremes, source-note ticks).
#
# Pulls a GHC on demand via `nix shell` (deliberately not in the dev shell), so
# normal `test`/CI never compile. Review and commit the resulting diff; expect
# churn on a nixpkgs bump (the dump dialect tracks the compiler version).
#
# Usage: gen-corpus.sh <ghc-core|ghc-stg|ghc-cmm>

set -euo pipefail

lang="${1:?usage: $0 <ghc-core|ghc-stg|ghc-cmm>}"
repo="$(cd "$(dirname "$0")/.." && pwd)"
out="$repo/tree-sitter-$lang/test/fixtures/dumps"

case "$lang" in
    ghc-core) dump=-ddump-simpl;     ext=dump-simpl ;;
    ghc-stg)  dump=-ddump-stg-final; ext=dump-stg-final ;;
    ghc-cmm)  dump=-ddump-cmm;       ext=dump-cmm ;;
    *) echo "unknown lang: $lang  (ghc-core|ghc-stg|ghc-cmm)" >&2; exit 64 ;;
esac

mkdir -p "$out"
# cd to repo so the source path GHC bakes into SourceNote ticks stays relative
# (test/fixtures/Foo.hs), i.e. reproducible across machines.
cd "$repo"

# gen <module> <tag> <extra-ghc-flags...>: dump <module> to <module>.<tag>.<ext>.
gen() {
    local mod="$1" tag="$2"; shift 2
    local tmp; tmp="$(mktemp -d)"
    nix shell --inputs-from "$repo" nixpkgs#ghc --command \
        ghc -c -fforce-recomp "$dump" -ddump-to-file \
            -dumpdir "$tmp" -outputdir "$tmp" "$@" "test/fixtures/$mod.hs" >/dev/null
    # GHC mirrors the source's relative path under -dumpdir; locate the dump.
    local f; f="$(find "$tmp" -name "$mod.$ext" -type f | head -1)"
    # Keep from the dump's banner onward and drop GHC's wall-clock timestamp
    # line, so committed files don't churn on every regen (uniques are
    # deterministic per compiler, so they don't).
    sed -En '/^={4,}/,$p' "$f" \
        | grep -vE '^[0-9]{4}-[0-9]{2}-[0-9]{2} .*UTC$' \
        > "$out/$mod.$tag.$ext"
    rm -rf "$tmp"
}

gen Bindings bare             -O2
gen Bindings suppress-uniques -O2 -dsuppress-uniques
# -dsuppress-all and SourceNote ticks (-g3) only meaningfully shape the Core
# pretty-printer's output; skip them for STG/Cmm.
if [[ "$lang" == ghc-core ]]; then
    gen Bindings suppress-all -O2 -dsuppress-all
    gen Ticks    ticks        -O  -g3
fi

nix shell --inputs-from "$repo" nixpkgs#ghc --command ghc --numeric-version \
    | sed 's/^/generated with GHC /'
echo "wrote $(ls "$out" | wc -l) $ext dumps to ${out#"$repo"/}; review & commit"
