#!/usr/bin/env bash
# Disposable regular images only. The same CLI cases run under AArch64 QEMU.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary=${UKE_RECOVERYCTL_BINARY:-"$component/build/ure-host/uke-recoveryctl"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
expect_error() {
    local code=$1; shift
    if "$binary" "$@" > "$work/error"; then echo 'Unexpected raw restore success' >&2; exit 1; fi
    jq -e --arg code "$code" '.error.code==$code' "$work/error" >/dev/null
}
for sector in 512 4096; do
    image="$work/restore $sector ' literal "'$(touch NEVER_RESTORE)'
    dd if=/dev/zero of="$image" bs=65536 count=5 status=none
    printf 'Verified raw image backup\n' | dd of="$image" conv=notrunc status=none
    desired=$(sha256sum "$image" | cut -d' ' -f1)
    "$binary" backup storage-plan --image "$image" --sector-size "$sector" --profile fixture --chunk-size 65536 --output "$work/backup-plan-$sector" >/dev/null
    "$binary" backup capture "$work/backup-plan-$sector" --journal "$work/backup-$sector" >/dev/null
    printf 'Before repair: preserve these bytes for rollback\n' | dd of="$image" conv=notrunc status=none
    before=$(sha256sum "$image" | cut -d' ' -f1)
    "$binary" restore plan "$work/backup-$sector" --image "$image" --sector-size "$sector" --profile fixture --output "$work/restore-plan-$sector" > "$work/review"
    jq -e --arg desired "$desired" --arg before "$before" '.data.operation=="storage.restore" and .data.target_sha256==$desired and .data.before.sha256==$before and .data.live_write_backend_ready==false and .data.host_streamed_restore==false and .data.physical_test_record==false' "$work/review" >/dev/null
    confirmation=$(jq -r '.plan_sha256' "$work/restore-plan-$sector")
    expect_error confirmation-required restore execute "$work/restore-plan-$sector" --image "$image" --sector-size "$sector" --journal "$work/rejected" --confirm wrong
    [[ ! -e $work/rejected ]]
    expect_error invalid-options restore plan "$work/backup-$sector" --image "$image" --sector-size "$sector" --profile fixture --output "$work/rejected" --root "$work"
    expect_error invalid-options restore plan "$work/backup-$sector" --image "$image" --object fake --profile fixture --output "$work/rejected"
    expect_error wrong-profile restore plan "$work/backup-$sector" --image "$image" --sector-size "$sector" --profile other --output "$work/rejected"
    expect_error invalid-sector restore plan "$work/backup-$sector" --image "$image" --sector-size 1024 --profile fixture --output "$work/rejected"
    cp -- "$image" "$work/foreign-image"
    expect_error wrong-target restore plan "$work/backup-$sector" --image "$work/foreign-image" --sector-size "$sector" --profile fixture --output "$work/rejected"
    jq '.target_sha256="forged"' "$work/restore-plan-$sector" > "$work/forged"
    expect_error invalid-restore-plan restore execute "$work/forged" --image "$image" --sector-size "$sector" --journal "$work/rejected" --confirm "$confirmation"
    for field in target_sha256 backup_plan_sha256 plan_sha256; do
        jq --arg field "$field" '.[$field]={bad:"type"}' "$work/restore-plan-$sector" > "$work/forged"
        expect_error invalid-restore-plan restore execute "$work/forged" --image "$image" --sector-size "$sector" --journal "$work/rejected" --confirm "$confirmation"
    done
    [[ $before == "$(sha256sum "$image" | cut -d' ' -f1)" ]]
    "$binary" restore execute "$work/restore-plan-$sector" --image "$image" --sector-size "$sector" --journal "$work/journal-$sector" --confirm "$confirmation" | jq -e '.data.state=="COMMITTED" and .data.verified==true' >/dev/null
    [[ $desired == "$(sha256sum "$image" | cut -d' ' -f1)" ]]
    "$binary" restore inspect "$work/journal-$sector" --image "$image" --sector-size "$sector" | jq -e '.data.classification=="TARGET" and .data.backup_verified==true and (.data.recovery_actions|index("rollback"))!=null' >/dev/null
    # Journal phase can lag a verified target. Original source is no longer needed.
    mv -- "$work/backup-$sector" "$work/offline-$sector"
    jq '.state="VERIFYING" | .completed_bytes=0' "$work/journal-$sector/journal.json" > "$work/state"
    cat "$work/state" > "$work/journal-$sector/journal.json"
    "$binary" restore resume "$work/journal-$sector" --image "$image" --sector-size "$sector" --confirm "$confirmation" | jq -e '.data.state=="COMMITTED" and .data.recovered_by_readback==true and .data.written_chunks==0' >/dev/null
    "$binary" restore rollback "$work/journal-$sector" --image "$image" --sector-size "$sector" --confirm "$confirmation" | jq -e '.data.state=="ROLLED_BACK" and .data.verified==true' >/dev/null
    [[ $before == "$(sha256sum "$image" | cut -d' ' -f1)" ]]
    "$binary" restore inspect "$work/journal-$sector" --image "$image" --sector-size "$sector" | jq -e '.data.classification=="ORIGINAL"' >/dev/null
    expect_error unsafe-resume restore resume "$work/journal-$sector" --image "$image" --sector-size "$sector" --confirm "$confirmation"
done
[[ ! -e NEVER_RESTORE ]]
printf 'Raw restore CLI: 512/4096 plans, identity/profile refusal, exact confirmation, persisted JSON, full readback, source-independent journal recovery and rollback passed; synthetic images only.\n'
