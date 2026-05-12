#!/usr/bin/env bash
# Emit paths to every *.cabal file in the configured corpora ($CABAL_SRC
# and $HLS_SRC). Used as the corpus selector for the cabal grammar.
set -o pipefail
exec "$(dirname "$0")/find-corpus.sh" \
    --root "$CABAL_SRC" \
    --root "$HLS_SRC" \
    --include '*.cabal' \
    --deny 'Cabal-tests/tests/ParserTests/errors/*' \
    --deny 'Cabal-tests/tests/ParserTests/ipi/*'
