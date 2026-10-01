#!/usr/bin/env bash
# Synthetic regular storage images and incomplete sysfs only. No live writes.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary=${UKE_RECOVERYCTL_BINARY:-"$component/build/ure-host/uke-recoveryctl"}
host_cli="$component/build/ure-host/uke-recoveryctl"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
expect_error() {
    local code=$1; shift
    if "$binary" "$@" > "$work/error"; then echo 'Unexpected storage backup success' >&2; exit 1; fi
    jq -e --arg code "$code" '.error.code==$code' "$work/error" >/dev/null
}
for sector in 512 4096; do
    image="$work/image $sector ' literal "'$(touch NEVER_STORAGE)'
    dd if=/dev/zero of="$image" bs=65536 count=5 status=none
    printf 'Synthetic storage source\n' | dd of="$image" conv=notrunc status=none
    before=$(sha256sum "$image" | cut -d' ' -f1)
    "$binary" backup storage-plan --image "$image" --sector-size "$sector" --profile fixture --chunk-size 65536 --output "$work/plan-$sector" > "$work/result"
    jq -e --argjson sector "$sector" '.data.schema==2 and .data.source_kind=="storage-image" and .data.source_identity.logical_sector_bytes==$sector and (.data.chunks|length)==5 and .data.atomic_snapshot==false and .data.restore_authorized==false and .data.physical_test_record==false' "$work/result" >/dev/null
    "$binary" backup capture "$work/plan-$sector" --journal "$work/store-$sector" | jq -e '.data.state=="COMPLETE" and .data.source_current_checked==true and .data.source_kind=="storage-image" and .data.atomic_snapshot==false' >/dev/null
    unlink "$work/store-$sector/chunk-00004.bin"
    "$binary" backup resume "$work/store-$sector" | jq -e '.data.verified==true and .data.source_current_checked==true' >/dev/null
    "$binary" backup export "$work/plan-$sector" --chunk 4 > "$work/chunk"
    [[ $(sha256sum "$work/chunk" | cut -d' ' -f1) == "$(jq -r '.chunks[4].sha256' "$work/plan-$sector")" ]]
    bash "$component/scripts/receive-backup.sh" --manifest "$work/plan-$sector" --output "$work/receiver-$sector" \
        --source-plan "$work/plan-$sector" --source-cli "$binary" --host-cli "$host_cli" --local > "$work/received"
    jq -e '.data.verified==true and .data.restore_authorized==false' "$work/received" >/dev/null
    expect_error invalid-options backup capture "$work/plan-$sector" --root "$work" --journal "$work/wrong-context"
    [[ ! -e $work/wrong-context ]]
    expect_error invalid-options backup storage-plan --image "$image" --object fake --profile fixture --output "$work/rejected"
    [[ ! -e $work/rejected ]]
    expect_error invalid-sector backup storage-plan --image "$image" --sector-size 1024 --profile fixture --output "$work/rejected"
    [[ $before == "$(sha256sum "$image" | cut -d' ' -f1)" ]]
    # Identical bytes in a replacement inode are a different source.
    cp -- "$image" "$work/replacement"; mv -- "$work/replacement" "$image"
    expect_error stale-source backup resume "$work/store-$sector"
    # Offline verification remains meaningful after the source disappears.
    unlink "$image"
    "$binary" backup verify "$work/store-$sector" | jq -e '.data.verified==true and .data.source_current_checked==false' >/dev/null
done
# A live-kind manifest without device-unit identity refuses before block access,
# irrespective of its checksum. It cannot borrow a regular-image identity.
jq '.source_kind="live-block" | .source_identity.kind="live-block" | .source_identity.unit_identity_available=false' "$work/plan-4096" > "$work/no-unit"
expect_error invalid-backup backup capture "$work/no-unit" --journal "$work/no-unit-store"
[[ ! -e $work/no-unit-store ]]
mkfifo "$work/fifo"
expect_error invalid-image backup storage-plan --image "$work/fifo" --profile fixture --output "$work/rejected"
if "$binary" backup export "$work/plan-4096" --chunk 0 --root "$work" > "$work/binary-error" 2> "$work/json-error"; then exit 1; fi
[[ ! -s $work/binary-error ]]
jq -e '.error.code=="invalid-options"' "$work/json-error" >/dev/null
[[ ! -e NEVER_STORAGE ]]
printf 'Storage-image backup: 512/4096 geometry, sealed identity, chunk/full hashes, capture/resume/export/receiver, inode replacement, offline verification, context and unit refusal passed; synthetic files only.\n'
