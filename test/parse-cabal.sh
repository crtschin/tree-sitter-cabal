#!/usr/bin/env bash
# Parse every *.cabal file under the given root and fail if any has an
# ERROR node.
#
# Usage: parse-cabal.sh <root>
set -o pipefail
"$(dirname "$0")/parse-corpus.sh" \
    --root "$1" \
    --include '*.cabal' \
    | tapview
