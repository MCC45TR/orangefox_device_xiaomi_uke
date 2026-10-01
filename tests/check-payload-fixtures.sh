#!/usr/bin/env bash
# Host-only negative fixtures for renamed runtimes and nested archive content.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
mkdir "$work/clean" "$work/bad" "$work/outer" "$work/payload"
printf '#!/bin/sh\nexit 0\n' > "$work/clean/helper"
bash "$component/tests/check-payload.sh" "$work/clean" >/dev/null
printf '#!/usr/bin/env python3\n' > "$work/bad/renamed-helper"
if bash "$component/tests/check-payload.sh" "$work/bad" >/dev/null 2>&1; then echo 'Renamed interpreter script accepted' >&2; exit 1; fi
printf 'PYTHONHOME\n' > "$work/bad/renamed-helper"
if bash "$component/tests/check-payload.sh" "$work/bad" >/dev/null 2>&1; then echo 'Renamed runtime identity accepted' >&2; exit 1; fi
(cd "$work/bad" && zip -q "$work/outer/inner.archive" renamed-helper)
(cd "$work/outer" && zip -q "$work/payload/outer.archive" inner.archive)
if bash "$component/tests/check-nested-payloads.sh" "$work/payload" >/dev/null 2>&1; then echo 'Nested forbidden payload accepted' >&2; exit 1; fi
printf 'ordinary safe content\n' > "$work/bad/renamed-helper"
rm "$work/outer/inner.archive" "$work/payload/outer.archive"
(cd "$work/bad" && zip -q "$work/outer/inner.archive" renamed-helper)
(cd "$work/outer" && zip -q "$work/payload/outer.archive" inner.archive)
bash "$component/tests/check-nested-payloads.sh" "$work/payload" | rg -q '2 archives'
printf 'Renamed Python/runtime and recursive archive fixtures: passed\n'
