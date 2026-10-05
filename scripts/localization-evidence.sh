#!/usr/bin/env bash
# Read-only host inventories. No generator, network, VM or tablet is executed.
set -euo pipefail
export LC_ALL=C
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source "$component/scripts/localization-evidence-lib.sh"
mode=${1:?source|ui PAYLOAD|verify-gui SOURCE_JSON UI_JSON REVIEW_JSON}
shift
mkdir -p "$component/build"
work=$(mktemp -d "$component/build/localization-inventory-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
case $mode in
    source) [[ $# == 0 ]]; localization_source "$component" "$work";;
    ui) [[ $# == 1 ]]; localization_ui "$component" "$1" "$work";;
    verify-gui) [[ $# == 3 ]]; localization_verify_gui "$1" "$2" "$3";;
    *) exit 2;;
esac
