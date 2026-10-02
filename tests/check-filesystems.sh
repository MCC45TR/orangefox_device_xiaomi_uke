#!/usr/bin/env bash
# Native host tools operate only on disposable regular images, with byte-exact rollback.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary=${UKE_RECOVERYCTL_BINARY:-$component/build/ure-host/uke-recoveryctl}
fixture=$(mktemp -d /tmp/ure-filesystems-XXXXXX)
trap 'find "$fixture" -depth -delete' EXIT
call() {
    if ! "$binary" "$@" > "$fixture/result.json" || ! jq -e '.result=="ok"' "$fixture/result.json" >/dev/null; then
        cat "$fixture/result.json" >&2
        return 1
    fi
}
fail() { if "$binary" "$@" > "$fixture/rejected.json"; then echo 'Expected a rejected filesystem operation' >&2; exit 1; fi; }
for type in ext4 vfat exfat ntfs f2fs btrfs; do
    mkdir "$fixture/$type"
    image="$fixture/$type/source.img"
    capacity=134217728
    if [[ $type == f2fs || $type == vfat ]]; then capacity=536870912; fi
    truncate -s "$capacity" "$image"
    inode=$(stat -c %i "$image")
    original=$(sha256sum "$image" | cut -d' ' -f1)
    jq -n --arg type "$type" '{schema:1,action:"format",filesystem:$type,erase_confirmed:true,label:"URETEST"}' > "$fixture/$type/request.json"
    call filesystem plan "$fixture/$type/request.json" --image "$image" --profile fixture-profile --output "$fixture/$type/plan.json"
    hash=$(jq -r .plan_sha256 "$fixture/$type/plan.json")
    fail filesystem execute "$fixture/$type/plan.json" --image "$image" --journal "$fixture/$type/wrong" --confirm bad
    [[ ! -e $fixture/$type/wrong && $(sha256sum "$image" | cut -d' ' -f1) == "$original" ]]
    call filesystem execute "$fixture/$type/plan.json" --image "$image" --journal "$fixture/$type/job" --confirm "$hash"
    jq -e '.data.state=="COMPLETE" and .data.physical_test_record==false' "$fixture/result.json" >/dev/null
    call filesystem inspect --image "$image"
    jq -e --arg type "$type" '.data.type==$type' "$fixture/result.json" >/dev/null
    call filesystem inspect-journal "$fixture/$type/job" --image "$image"
    jq -e '.data.application.classification=="TARGET" and any(.data.application.recovery_actions[]; .=="rollback")' "$fixture/result.json" >/dev/null
    if [[ $type == ext4 || $type == ntfs || $type == vfat || $type == f2fs ]]; then
        formatted=$(sha256sum "$image" | cut -d' ' -f1)
        resized_bytes=$((capacity/2))
        if [[ $type == vfat ]]; then
            resized_bytes=$((384*1024*1024))
            jq -n '{schema:1,action:"resize",filesystem:"vfat",target_bytes:67108864}' > "$fixture/$type/unsafe-resize.json"
            fail filesystem plan "$fixture/$type/unsafe-resize.json" --image "$image" --profile fixture-profile --output "$fixture/$type/unsafe-plan.json"
            [[ ! -e $fixture/$type/unsafe-plan.json ]]
            cp --reflink=auto "$image" "$fixture/$type/empty-formatted.img"
            printf 'Verified FAT resize data\n' > "$fixture/$type/payload.txt"
            mcopy -i "$image" "$fixture/$type/payload.txt" ::PAYLOAD.TXT
        fi
        formatted=$(sha256sum "$image" | cut -d' ' -f1)
        jq -n --arg type "$type" --argjson bytes "$resized_bytes" '{schema:1,action:"resize",filesystem:$type,target_bytes:$bytes}' > "$fixture/$type/resize.json"
        call filesystem plan "$fixture/$type/resize.json" --image "$image" --profile fixture-profile --output "$fixture/$type/resize-plan.json"
        resize_hash=$(jq -r .plan_sha256 "$fixture/$type/resize-plan.json")
        call filesystem execute "$fixture/$type/resize-plan.json" --image "$image" --journal "$fixture/$type/resize-job" --confirm "$resize_hash"
        [[ $(stat -c %s "$image") == "$capacity" && $(stat -c %i "$image") == "$inode" ]]
        if [[ $type == vfat ]]; then
            mcopy -i "$image" ::PAYLOAD.TXT "$fixture/$type/readback.txt"
            cmp "$fixture/$type/payload.txt" "$fixture/$type/readback.txt"
        fi
        call filesystem rollback "$fixture/$type/resize-job" --image "$image" --confirm "$resize_hash"
        [[ $(sha256sum "$image" | cut -d' ' -f1) == "$formatted" ]]
        if [[ $type == vfat ]]; then
            # Undo independent payload setup before testing the format journal.
            dd if="$fixture/$type/empty-formatted.img" of="$image" bs=1M conv=notrunc status=none
        fi
    fi
    call filesystem rollback "$fixture/$type/job" --image "$image" --confirm "$hash"
    [[ $(sha256sum "$image" | cut -d' ' -f1) == "$original" ]]
    printf '%s\n' "$type format, independent check, original inode preservation and complete rollback passed."
done
call filesystem capabilities
jq -e '.data.filesystems[] | select(.filesystem=="exfat") | .offline_resize_available==false' "$fixture/result.json" >/dev/null
fail filesystem capabilities --confirm unused
fail filesystem plan "$fixture/ext4/request.json" --image "$fixture/ext4/source.img" --profile fixture-profile --output "$fixture/invalid-plan" --journal unused
printf '%s\n' 'Filesystem tool transactions and strict CLI selection fixtures passed.'
