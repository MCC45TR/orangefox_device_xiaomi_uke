#!/usr/bin/env bash
# Native gate regressions. Only temporary regular-file fixtures are mutated.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tree="$component/src/upstream/orangefox-android16"
first_guard() {
    local source=$1 signature=$2 operation=$3
    awk -v signature="$signature" -v operation="$operation" '
      index($0,signature)==1 { if (seen++) exit 2; copying=1 }
      copying && !opened { if (index($0,"{")) opened=1; next }
      copying && opened && !checked && $0 !~ /^[[:space:]]*$/ {
        if ($0 !~ /ure_legacy_write_guard|legacy_write_decision/ || !index($0,"ure::LegacyWrite::" operation)) exit 3
        checked=1; copying=0
      }
      END { if (seen!=1 || !checked) exit 4 }
    ' "$source"
}
count=0
while IFS=$'\t' read -r source signature operation; do
    [[ -z $source || $source == \#* ]] && continue
    first_guard "$tree/$source" "$signature" "$operation" || {
        printf 'Write guard is missing or delayed: %s\n' "$signature" >&2; exit 1;
    }
    count=$((count+1))
done < "$component/configs/ure/legacy-write-entry-points.tsv"
[[ $count -ge 80 ]]
cmp "$component/src/device/xiaomi/uke/ure-write-gate.hpp" "$tree/bootable/recovery/ure-write-gate.hpp"
cmp "$component/src/device/xiaomi/uke/recoveryctl/libuke/recovery_write_policy.hpp" "$tree/device/xiaomi/uke/recoveryctl/libuke/recovery_write_policy.hpp"
git -C "$tree/bootable/recovery" apply --reverse --check "$component/patches/0020-shared-recovery-write-gate.patch"
git -C "$tree/system/core" apply --reverse --check "$component/patches/0021-fastbootd-write-gate.patch"
awk '$2=="/data" || $2=="/metadata" { if ($4 !~ /(^|,)ro(,|$)/ || $4 !~ /(^|,)norecovery(,|$)/) exit 1; n++ }
     $2=="/persist" { if ($4 !~ /(^|,)ro(,|$)/ || $4 !~ /(^|,)noload(,|$)/) exit 1; p++ }
     END { if (n!=2 || p!=1) exit 1 }' "$component/src/device/xiaomi/uke/recovery/root/system/etc/recovery.fstab"
temporary=$(mktemp -d)
trap 'rm -r -- "$temporary"' EXIT
bash "$component/tests/generate-write-gate-hooks.sh" "$temporary/actual-hooks.cpp"
sed '/ure_legacy_write_guard(ure::LegacyWrite::Format)/d' "$temporary/actual-hooks.cpp" > "$temporary/missing-format-guard.cpp"
if first_guard "$temporary/missing-format-guard.cpp" 'int TWPartitionManager::Format_Data(void)' Format; then
    echo 'Gate census accepted a deliberately removed Format Data guard' >&2; exit 1
fi
compiler=${CXX:-c++}
"$compiler" -std=c++20 -Wall -Wextra -Werror -Wno-unused-parameter \
    -DAB_OTA_UPDATER -DOF_REFRESH_ENCRYPTION_PROPS_BEFORE_FORMAT -DOF_WIPE_METADATA_AFTER_DATAFORMAT \
    -I"$component/tests/ure" -I"$component/tests/ure/write-gate-mocks" -I"$component/src/device/xiaomi/uke" \
    -I"$component/src/device/xiaomi/uke/recoveryctl/libuke" \
    "$component/tests/ure/legacy_write_gate.cpp" "$temporary/missing-format-guard.cpp" -o "$temporary/mutant"
if "$temporary/mutant" > "$temporary/mutant.log" 2>&1; then
    echo 'Native fixture accepted a deliberately removed Format Data guard' >&2; exit 1
fi
rg -q 'Format Data reported success|performed pre-denial|unmounted metadata' "$temporary/mutant.log"
printf '%s native entry guards and a compiled removed-guard regression passed.\n' "$count"
