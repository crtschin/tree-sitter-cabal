#!/usr/bin/env bash
# Walk a directory, find files matching a set of name globs, and parse each
# one with `tree-sitter parse` (using the grammar in the current working
# directory). Emits TAP version 14 on stdout. Exits non-zero if any file
# fails to parse.
#
# Usage:
#   parse-corpus.sh --root <dir> --include <glob> [--include <glob>...]
#                   [--exclude <glob>...] [--deny <relpath>...]
#
# The script must be invoked from inside the grammar's directory (the one
# containing tree-sitter.json) so `tree-sitter parse` picks the right parser.

set -uo pipefail

usage() {
    sed -n '2,/^$/p' "$0" | sed 's/^# \?//' >&2
    exit 64
}

root=
includes=()
excludes=()
deny=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --root)    root="$2"; shift 2 ;;
        --include) includes+=("$2"); shift 2 ;;
        --exclude) excludes+=("$2"); shift 2 ;;
        --deny)    deny+=("$2"); shift 2 ;;
        -h|--help) usage ;;
        *)         echo "unknown arg: $1" >&2; usage ;;
    esac
done

[[ -z "$root" ]] && { echo "--root is required" >&2; usage; }
[[ ${#includes[@]} -eq 0 ]] && { echo "at least one --include is required" >&2; usage; }
[[ ! -d "$root" ]] && { echo "root not found: $root" >&2; exit 1; }

find_args=("$root" -type f \( )
for i in "${!includes[@]}"; do
    [[ $i -gt 0 ]] && find_args+=( -o )
    find_args+=( -name "${includes[$i]}" )
done
find_args+=( \) )
for ex in "${excludes[@]}"; do
    find_args+=( ! -name "$ex" )
done
find_args+=( ! -path '*/.git/*' )

mapfile -t found < <(find "${find_args[@]}" | LC_ALL=C sort)

files=()
for f in "${found[@]}"; do
    rel="${f#"$root"/}"
    skip=0
    for d in "${deny[@]}"; do
        if [[ "$rel" == "$d" ]]; then skip=1; break; fi
    done
    [[ $skip -eq 0 ]] && files+=("$f")
done

n=${#files[@]}
echo "TAP version 14"
echo "1..$n"
if [[ $n -eq 0 ]]; then
    echo "Bail out! no files matched under $root"
    exit 1
fi

# Single-pass batch parse; capture any per-file failure lines.
parse_output=$(tree-sitter parse --quiet "${files[@]}" 2>&1) || true

declare -A error_for=()
while IFS= read -r line; do
    # Failure lines look like:
    #   <path>\tParse: <ms>\t<bytes>/ms\t(ERROR [r1, c1] - [r2, c2])
    [[ -z "$line" ]] && continue
    if [[ "$line" == *$'\t'Parse:* ]]; then
        path="${line%%$'\t'*}"
        detail="${line##*$'\t'}"
        error_for["$path"]="$detail"
    fi
done <<< "$parse_output"

exit_code=0
i=0
for f in "${files[@]}"; do
    i=$((i+1))
    rel="${f#"$root"/}"
    if [[ -n "${error_for[$f]:-}" ]]; then
        echo "not ok $i - $rel"
        echo "  ---"
        echo "  error: ${error_for[$f]}"
        echo "  ..."
        exit_code=1
    else
        echo "ok $i - $rel"
    fi
done

exit $exit_code
