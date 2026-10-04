#!/usr/bin/env bash
# Explicit host release classes; no target execution or device effects.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source "$component/scripts/release-policy-lib.sh"
policy="$component/configs/release-policy.json"
mode=${1:?plan|normalize|validate|preflight}
shift
case $mode in
    plan)
        [[ $# == 1 ]]
        jq -ne --arg class "$1" --slurpfile policy "$policy" \
            'if ($policy[0].classes[$class]|type)=="object" then
              {schema_version:1,release_class:$class,capabilities:$policy[0].shipping_capabilities}
             else error("Unknown release class") end'
        ;;
    normalize) [[ $# == 1 ]]; release_normalize "$policy" "$1";;
    validate) [[ $# == 1 ]]; release_validate_plan "$policy" "$1";;
    preflight) [[ $# == 3 ]]; release_check_receipts "$policy" "$1" "$2" "$3";;
    native-pair) [[ $# == 5 ]]; release_native_pair "$@";;
    *) exit 2;;
esac
