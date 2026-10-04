#!/usr/bin/env bash
# One disposable persistent domain per independent host fixture invocation.
# All children of this invocation inherit the same domain; no PID/thread bypass.
set -euo pipefail
build=${1:?Persistent fixture build directory is required}
shift
[[ -d $build && $# -gt 0 ]]
build=$(realpath -e -- "$build")
scope=$(mktemp -d "$build/operation-fixture-XXXXXX")
chmod 0700 "$scope"
export URE_OPERATION_COORDINATOR="$scope/coordinator"
set +e
"$@"
result=$?
set -e
if [[ -f $URE_OPERATION_COORDINATOR/owner.json ]] ||
   [[ -f $URE_OPERATION_COORDINATOR/release.json && $(jq -r '.phase // "unknown"' "$URE_OPERATION_COORDINATOR/release.json") != RELEASED ]]; then
    printf 'Fixture retained unresolved ownership; private evidence kept at %s\n' "$scope" >&2
    [[ $result != 0 ]] || result=1
fi
if [[ $result == 0 ]]; then
    # Only the mktemp directory created by this invocation is removed.
    rm -rf -- "$scope"
else
    printf 'Private operation fixture directory retained: %s\n' "$scope" >&2
fi
exit "$result"
