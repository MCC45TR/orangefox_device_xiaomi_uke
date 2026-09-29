#!/usr/bin/env bash
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary="$component/build/recoveryctl/uke-recoveryctl"
fixture="$component/tests/fixtures/recovery-block"

"$binary" list "$fixture" | rg -q $'sda1\tuke_esp\t12345678-1234-1234-1234-123456789abc'
"$binary" plan-mount esp 12345678-1234-1234-1234-123456789abc "$fixture" | rg -q 'read-only.*\/mnt\/uke-esp.*vfat'
"$binary" plan-mount linux abcdefab-cdef-abcd-efab-cdefabcdefab "$fixture" | rg -q 'read-only.*\/mnt\/uke-linux.*ext4'
"$binary" plan-mount linux-btrfs abcdefab-cdef-abcd-efab-cdefabcdefab "$fixture" | rg -q 'read-only.*\/mnt\/uke-linux.*btrfs'
if "$binary" plan-mount esp abcdefab-cdef-abcd-efab-cdefabcdefab "$fixture" >/dev/null 2>&1; then
  echo 'Wrong partition role was accepted' >&2; exit 1
fi
if "$binary" plan-mount esp bad-uuid "$fixture" >/dev/null 2>&1; then
  echo 'Malformed PARTUUID was accepted' >&2; exit 1
fi
if "$binary" mount-ro esp 12345678-1234-1234-1234-123456789abc "$fixture" >/dev/null 2>&1; then
  echo 'Extra argument was accepted for live mount' >&2; exit 1
fi
scratch=$(mktemp -d)
trap 'rm -r -- "$scratch"' EXIT
cp -a -- "$fixture/." "$scratch/"
cp -a -- "$scratch/sda1" "$scratch/sda3"
if "$binary" plan-mount esp 12345678-1234-1234-1234-123456789abc "$scratch" >/dev/null 2>&1; then
  echo 'Ambiguous partition identity was accepted' >&2; exit 1
fi
printf 'Recovery identity and read-only mount-plan fixtures: passed\n'
