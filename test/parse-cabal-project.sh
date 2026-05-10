#!/usr/bin/env bash
# Parse every *.project / cabal.project.* file under the given root and fail
# if any has an ERROR node.
#
# Usage: parse-cabal-project.sh <root>
set -o pipefail
"$(dirname "$0")/parse-corpus.sh" \
    --root "$1" \
    --include '*.project' \
    --include 'cabal.project.*' \
    --exclude '*.hs' \
    --exclude '*.out' \
    --exclude '*.lock' \
    --deny 'cabal-testsuite/PackageTests/ProjectConfig/FieldStanzaConfusion/cabal.project' \
    | tapview
