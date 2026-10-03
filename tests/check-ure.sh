#!/usr/bin/env bash
# Host fixtures only. Never opens a live block path or invokes a write tool.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary=${UKE_RECOVERYCTL_BINARY:-"$component/build/ure-host/uke-recoveryctl"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
"$binary" help | jq -e '.schema==1 and .result=="ok" and (.data.commands|length)>20' >/dev/null
expect_error() {
    local expected=$1; shift
    if "$binary" "$@" > "$work/result"; then echo 'Unexpected CLI success' >&2; exit 1; fi
    jq -e --arg code "$expected" '.schema==1 and .result=="error" and .error.code==$code' "$work/result" >/dev/null
}
expect_error invalid-options diagnose all --confirm ignored
expect_error invalid-options help --json --json
expect_error root-required linux detect
expect_error root-required btrfs subvolumes
expect_error invalid-image filesystem check --image /dev/zero
expect_error unknown-command unsupported-action
mkdir -p "$work/root/usr/lib" "$work/root/etc" "$work/system"
printf 'ID=fedora\nNAME="Fedora Linux"\n' > "$work/root/usr/lib/os-release"
printf 'root / ext4 defaults 0 1\n' > "$work/root/etc/fstab"
printf 'root / ext4 ro 0 1\n' > "$work/new"
"$binary" linux detect --root "$work/root" | jq -e '.data.distribution.ID=="fedora" and .data.recovery_kernel_is_installed_kernel==false' >/dev/null
"$binary" capabilities --system-root "$work/system" | jq -e '(.data.capabilities|length)==24 and .data.physical_test_record==false' >/dev/null
"$binary" capabilities --system-root "$work/system" | jq -e \
    '.data.active_scope.remote_transport=="adb-only" and (.data.active_scope.bitlocker==false) and (.data.active_scope.ssh_sftp==false) and (.data.active_scope.network_rescue==false)' >/dev/null
"$binary" editor plan etc/fstab --root "$work/root" --content-file "$work/new" --profile fixture --output "$work/plan" >/dev/null
"$binary" transaction validate "$work/plan" --root "$work/root" | jq -e '.data.valid==true' >/dev/null
confirmation=$(jq -r .plan_sha256 "$work/plan")
expect_error confirmation-required transaction execute "$work/plan" --root "$work/root" --journal "$work/rejected" --confirm incorrect
[[ ! -e $work/rejected ]]
"$binary" transaction execute "$work/plan" --root "$work/root" --journal "$work/journal" --confirm "$confirmation" | jq -e '.data.state=="COMMITTED"' >/dev/null
cmp "$work/new" "$work/root/etc/fstab"
"$binary" transaction inspect "$work/journal" --root "$work/root" | jq -e '.data.current_state=="PAYLOAD_VERIFIED" and .data.incomplete==false and .data.backup_verified==true' >/dev/null
[[ -s $work/journal/plan.json ]]
expect_error unsafe-resume transaction resume "$work/journal" --root "$work/root" --confirm "$confirmation"
jq '.state="VERIFYING" | del(.result_identity,.verified)' "$work/journal/journal.json" > "$work/interrupted-journal"
chmod 600 "$work/interrupted-journal"
mv -- "$work/interrupted-journal" "$work/journal/journal.json"
before_inode=$(stat -c %i "$work/root/etc/fstab")
"$binary" transaction resume "$work/journal" --root "$work/root" --confirm "$confirmation" | jq -e '.data.state=="COMMITTED" and .data.recovered_by_readback==true' >/dev/null
[[ $before_inode == "$(stat -c %i "$work/root/etc/fstab")" ]]
"$binary" transaction rollback "$work/journal" --root "$work/root" --confirm "$confirmation" | jq -e '.data.state=="ROLLED_BACK"' >/dev/null
printf 'root / ext4 defaults 0 1\n' > "$work/expected"
cmp "$work/expected" "$work/root/etc/fstab"
printf 'URE CLI JSON, invalid inputs, inspected journals, readback-only resume and rollback: passed\n'
