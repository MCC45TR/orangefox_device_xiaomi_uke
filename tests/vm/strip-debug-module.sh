#!/usr/bin/env bash
# Host-only staging for the generic formatter VM. The original stays untouched.
# Never use this test helper to prepare a replacement tablet/vendor module.
set -euo pipefail
umask 077
component=${1:?Component}
original=${2:?Original generic VM module}
staged=${3:?Private staged module}
record=${4:?Evidence directory}
[[ $# == 4 && -f $original && ! -L $original && -d $record && ! -L $record &&
   ! -e $staged && ! -L $staged ]]
if tail -c 28 "$original" | rg -aq '~Module signature appended~'; then
  echo 'Signed generic modules cannot be modified by this test helper.' >&2
  exit 1
fi
readelf -h "$original" > "$record/module-elf-header.txt"
rg -q 'Type:.*REL ' "$record/module-elf-header.txt"
rg -q 'Machine:.*AArch64' "$record/module-elf-header.txt"
strip_tool=$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/llvm-strip
[[ $(sha256sum "$strip_tool" | cut -d' ' -f1) == d2a3191ad2228bb60c35e18466615cb53ed7263c2e73134624349f0367cb1f88 ]]
sha256sum "$original" "$strip_tool" > "$record/module-original.sha256"
"$strip_tool" --strip-debug -o "$staged" "$original"
section_metadata() {
  readelf -SW "$1" | sed -E 's/^[[:space:]]*\[[[:space:]]*[0-9]+\][[:space:]]*/ /' |
    awk '$7~/A/ {print $1,$2,$3,$5,$6,$7,$10}'
}
section_metadata "$original" > "$record/module-allocated-before.txt"
section_metadata "$staged" > "$record/module-allocated-after.txt"
cmp "$record/module-allocated-before.txt" "$record/module-allocated-after.txt"
while read -r section kind rest; do
  [[ $kind != NOBITS ]] || continue
  readelf -x "$section" "$original" > "$record/module-section-before.txt"
  readelf -x "$section" "$staged" > "$record/module-section-after.txt"
  cmp "$record/module-section-before.txt" "$record/module-section-after.txt"
  sha256sum "$record/module-section-before.txt" | cut -d' ' -f1 | tr '\n' ' '
  printf '%s\n' "$section"
done < "$record/module-allocated-before.txt" > "$record/module-allocated-content.sha256"
readelf -x .modinfo "$original" > "$record/module-modinfo-before.txt"
readelf -x .modinfo "$staged" > "$record/module-modinfo-after.txt"
cmp "$record/module-modinfo-before.txt" "$record/module-modinfo-after.txt"
relocations() {
  readelf -Wr "$1" | awk '/^Relocation section/ {debug=($3~/debug/); if(!debug)print $3; next}
    !debug && $1~/^[0-9a-f]+$/ {$2=""; $1=$1; print}'
}
relocations "$original" > "$record/module-relocations-before.txt"
relocations "$staged" > "$record/module-relocations-after.txt"
cmp "$record/module-relocations-before.txt" "$record/module-relocations-after.txt"
readelf -SW "$staged" | rg -q ' \.symtab '
readelf -SW "$staged" | rg -q ' \.strtab '
sha256sum -c "$record/module-original.sha256" > "$record/module-original-repeat.log"
sha256sum "$staged" > "$record/module-staged.sha256"
