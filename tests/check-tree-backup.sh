#!/usr/bin/env bash
# Linux/home archive fixtures only. Never mount storage or contact a tablet.
set -euo pipefail
component=$(cd -- "$(dirname -- "$0")/.." && pwd)
binary=$component/build/ure-host/uke-recoveryctl
if [[ -v UKE_RECOVERYCTL_BINARY ]]; then binary=$UKE_RECOVERYCTL_BINARY; fi
work=$(mktemp -d)
capture_pid=
cleanup() { if [[ -n $capture_pid ]] && kill -0 "$capture_pid" 2>/dev/null; then kill -KILL "$capture_pid"; wait "$capture_pid" 2>/dev/null || true; fi; rm -rf -- "$work"; }
trap cleanup EXIT
reject() { if "$binary" "$@" > "$work/rejected.json" 2> "$work/rejected.err"; then echo 'Expected tree operation refusal' >&2; exit 1; fi; }
source_dir="$work/Linux home 'fixture'"
mkdir -p "$source_dir/user/documents" "$work/output"
printf 'Synthetic home document\n' > "$source_dir/user/documents/data"
ln "$source_dir/user/documents/data" "$source_dir/user/hardlink"
ln -s /unavailable/external/source "$source_dir/user/absolute-link"
printf 'name fixture\n' > "$source_dir/user/line"$'\n'"break"
mkfifo -m 640 "$source_dir/user/pipe"
truncate -s 16M "$source_dir/user/sparse"
printf 'extent\n' | dd of="$source_dir/user/sparse" bs=1 seek=8388608 conv=notrunc status=none
chmod 750 "$source_dir/user/documents"
touch -m -d '2024-01-02 03:04:05.123456789 UTC' "$source_dir/user/documents"
acl_test=false
if command -v setfacl >/dev/null && command -v getfacl >/dev/null; then
    # The QEMU fixture runs in a user namespace; use an actually mapped UID.
    # Named-user and inherited default ACLs must still round-trip exactly.
    fixture_uid=$(id -u)
    setfacl -m "u:$fixture_uid:r--" "$source_dir/user/documents/data"
    setfacl -m "d:u:$fixture_uid:r-x" "$source_dir/user/documents"
    acl_test=true
fi
"$binary" backup tree-plan user --root "$source_dir" --profile global-os3.0.303.0 --output "$work/store" > "$work/planned.json"
hash=$(jq -er '.data.plan_sha256' "$work/planned.json")
jq -e '.data.state=="PLANNED" and .data.atomic_snapshot==false and .data.entries==8' "$work/planned.json" >/dev/null
"$binary" backup tree-inspect "$work/store" | jq -e '.data.metadata_pages_verified and (.data.data_verified==false) and (.data.verified==false)' >/dev/null
reject backup tree-capture "$work/store" --root "$source_dir" --confirm wrong
reject backup tree-verify "$work/store" --root "$source_dir"
reject backup tree-inspect "$work/store" --image "$work/not-used"
reject backup tree-restore "$work/store" --output "$work/not-created" --confirm "$hash"
[[ ! -e $work/not-created ]]
"$binary" backup tree-capture "$work/store" --root "$source_dir" --confirm "$hash" > "$work/captured.json"
jq -e '.data.state=="COMPLETE" and .data.verified' "$work/captured.json" >/dev/null
"$binary" backup tree-verify "$work/store" | jq -e '.data.verified' >/dev/null
"$binary" backup tree-restore "$work/store" --output "$work/output/restored-tree" --confirm "$hash" > "$work/restored.json"
jq -e '.data.state=="RESTORED" and .data.verified and (.data.existing_files_overwritten==false)' "$work/restored.json" >/dev/null
cmp "$source_dir/user/documents/data" "$work/output/restored-tree/documents/data"
cmp "$source_dir/user/sparse" "$work/output/restored-tree/sparse"
cmp "$source_dir/user/line"$'\n'"break" "$work/output/restored-tree/line"$'\n'"break"
[[ $(stat -c %i "$work/output/restored-tree/documents/data") == $(stat -c %i "$work/output/restored-tree/hardlink") ]]
[[ $(readlink "$work/output/restored-tree/absolute-link") == /unavailable/external/source ]]
[[ -p $work/output/restored-tree/pipe && $(stat -c %a "$work/output/restored-tree/pipe") == 640 ]]
[[ $(stat -c '%a %y' "$source_dir/user/documents") == $(stat -c '%a %y' "$work/output/restored-tree/documents") ]]
[[ $(stat -c %b "$work/output/restored-tree/sparse") -lt 1000 ]]
if $acl_test; then
    getfacl -cp "$source_dir/user/documents/data" > "$work/acl-before"
    getfacl -cp "$work/output/restored-tree/documents/data" > "$work/acl-after"
    cmp "$work/acl-before" "$work/acl-after"
    getfacl -cp "$source_dir/user/documents" > "$work/dir-acl-before"
    getfacl -cp "$work/output/restored-tree/documents" > "$work/dir-acl-after"
    cmp "$work/dir-acl-before" "$work/dir-acl-after"
fi
reject backup tree-restore "$work/store" --output "$work/output/restored-tree" --confirm "$hash"
ln -s "$source_dir/user" "$work/source-alias"
reject backup tree-plan . --root "$work/source-alias" --profile global-os3.0.303.0 --output "$work/alias-store"
reject backup tree-plan . --root "$source_dir/user" --profile global-os3.0.303.0 --output "$source_dir/user/recursive-store"
[[ ! -e $source_dir/user/recursive-store ]]
# Source mutation invalidates capture without invalidating the completed backup.
printf 'changed\n' >> "$source_dir/user/documents/data"
reject backup tree-capture "$work/store" --root "$source_dir" --confirm "$hash"
"$binary" backup tree-verify "$work/store" | jq -e '.data.verified' >/dev/null
# Tampered pages and blobs refuse restore before creating a target.
cp "$work/store/entries-0.json" "$work/page-before"
printf 'bad\n' > "$work/store/entries-0.json"
reject backup tree-inspect "$work/store"
reject backup tree-restore "$work/store" --output "$work/bad-restore" --confirm "$hash"
[[ ! -e $work/bad-restore ]]
cp "$work/page-before" "$work/store/entries-0.json"
blob=$(find "$work/store" -maxdepth 1 -name 'data-*.bin' -print -quit)
printf 'corrupt\n' > "$blob"
reject backup tree-verify "$work/store"
reject backup tree-restore "$work/store" --output "$work/bad-restore" --confirm "$hash"
[[ ! -e $work/bad-restore ]]
# Kill only this fixture-owned capture after observing actual blob publication.
mkdir "$work/resume-source"
for ((index=0;index<650;index++)); do printf 'entry %s\n' "$index" > "$work/resume-source/file-$index"; done
"$binary" backup tree-plan . --root "$work/resume-source" --profile global-os3.0.303.0 --output "$work/resume-store" > "$work/resume-plan.json"
resume_hash=$(jq -er '.data.plan_sha256' "$work/resume-plan.json")
"$binary" backup tree-capture "$work/resume-store" --root "$work/resume-source" --confirm "$resume_hash" > "$work/interrupted.json" 2> "$work/interrupted.err" &
capture_pid=$!
observed=false
for ((attempt=0;attempt<1000;attempt++)); do
    if [[ -n $(find "$work/resume-store" -maxdepth 1 -name 'data-*.bin' -print -quit) ]] && kill -0 "$capture_pid" 2>/dev/null; then observed=true; break; fi
    kill -0 "$capture_pid" 2>/dev/null || break
    sleep 0.02
done
$observed || { echo 'Did not observe a live fixture capture with published data' >&2; exit 1; }
kill -KILL "$capture_pid"
if wait "$capture_pid" 2>/dev/null; then echo 'Interrupted capture unexpectedly succeeded' >&2; exit 1; fi
capture_pid=
"$binary" backup tree-inspect "$work/resume-store" | jq -e '.data.state=="CAPTURING" and (.data.verified==false)' >/dev/null
"$binary" backup tree-capture "$work/resume-store" --root "$work/resume-source" --confirm "$resume_hash" | jq -e '.data.state=="COMPLETE" and .data.verified' >/dev/null
"$binary" backup tree-verify "$work/resume-store" | jq -e '.data.verified' >/dev/null
printf 'Tree CLI: Linux/home metadata, ACLs where available, hardlinks, symlinks, sparse data, opaque names, independent verification, new-directory restore, refusals and observed SIGKILL/resume passed; synthetic files only.\n'
