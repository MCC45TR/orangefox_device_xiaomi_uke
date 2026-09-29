#!/usr/bin/env bash
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary="$component/build/inventory/uke-partition-inventory"
"$binary" "$component/tests/fixtures/valid-rawprogram.xml" | rg -q $'4\trecovery_a\t40\t40\t4096\trecovery.img\tresolved'
"$binary" "$component/tests/fixtures/valid-rawprogram.xml" | rg -q 'BackupGPT.*symbolic'
if "$binary" "$component/tests/fixtures/overlap-rawprogram.xml" >/dev/null 2>&1; then echo 'Overlap was accepted' >&2; exit 1; fi
if "$binary" "$component/tests/fixtures/overflow-rawprogram.xml" >/dev/null 2>&1; then echo 'Overflow was accepted' >&2; exit 1; fi
printf 'Read-only rawprogram inventory fixtures: passed\n'
