#!/usr/bin/env bash
# Emit paths to every cabal.project / *.project file in the configured
# corpora ($CABAL_SRC and $HLS_SRC). Used as the corpus selector for the
# cabal-project grammar.
set -o pipefail
exec "$(dirname "$0")/find-corpus.sh" \
    --root "$CABAL_SRC" \
    --root "$HLS_SRC" \
    --include '*.project' \
    --include 'cabal.project.*' \
    --exclude '*.hs' \
    --exclude '*.out' \
    --exclude '*.lock'
