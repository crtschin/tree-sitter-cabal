#!/usr/bin/env bash
# Thin shim selecting the ghc-core dump corpus. See ghc-dump-files.sh.
exec "$(dirname "$0")/ghc-dump-files.sh" ghc-core
