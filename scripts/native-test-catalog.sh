#!/usr/bin/env bash
# Names of the entire configured CTest suite, without private command paths.
set -euo pipefail
[[ $# == 1 && -d $1 && ! -L $1 ]]
ctest --test-dir "$1" --show-only=json-v1 | jq -Se '
    [.tests[].name]|sort|if length>0 and length==(unique|length) then .
    else error("Empty or duplicate CTest catalog") end'
