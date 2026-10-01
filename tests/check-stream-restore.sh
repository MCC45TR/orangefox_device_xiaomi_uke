#!/usr/bin/env bash
# Synthetic images and mock SSH/ADB only; no device or live block writes.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary=${UKE_RECOVERYCTL_BINARY:-"$component/build/ure-host/uke-recoveryctl"}
work=$(mktemp -d)
trap 'find "$work" -depth -mindepth 1 -delete; rmdir -- "$work"' EXIT
expect_error() {
    local code=$1; shift
    if "$binary" "$@" > "$work/error"; then echo 'Unexpected stream restore success' >&2; exit 1; fi
    jq -e --arg code "$code" '.error.code==$code' "$work/error" >/dev/null
}
for sector in 512 4096; do
    image="$work/image $sector ' literal "'$(touch NEVER_STREAM_RESTORE)'
    dd if=/dev/zero of="$image" bs=65536 count=3 status=none
    printf 'Known desired image\n' | dd of="$image" conv=notrunc status=none
    desired=$(sha256sum "$image" | cut -d' ' -f1)
    "$binary" backup storage-plan --image "$image" --sector-size "$sector" --profile fixture --chunk-size 65536 --output "$work/after-plan-$sector" >/dev/null
    "$binary" backup capture "$work/after-plan-$sector" --journal "$work/after-$sector" >/dev/null
    printf 'Original image to preserve on host\n' | dd of="$image" conv=notrunc status=none
    original=$(sha256sum "$image" | cut -d' ' -f1)
    plan="$work/stream-plan-$sector"
    "$binary" restore stream-plan "$work/after-plan-$sector" --image "$image" --sector-size "$sector" --profile fixture --output "$plan" > "$work/review"
    jq -e '.data.operation=="storage.stream-restore" and .data.host_streamed_restore and (.data.live_write_backend_ready==false) and (.data.physical_test_record==false)' "$work/review" >/dev/null
    confirmation=$(jq -er '.plan_sha256' "$plan")
    expect_error invalid-options restore stream-plan "$work/after-plan-$sector" --image "$image" --profile fixture --output "$work/rejected" --receipt wrong
    expect_error invalid-options restore stream-backup-plan "$plan" --image "$image" --output "$work/rejected"
    expect_error invalid-options restore host-receipt "$plan" --before "$work/none" --after "$work/none" --system-root / --output "$work/rejected"
    # Binary export places its JSON error on stderr, preserving binary stdout.
    if "$binary" backup store-export "$work/after-$sector" --chunk 0 --root / > "$work/binary" 2> "$work/error"; then exit 1; fi
    [[ ! -s $work/binary ]]; jq -e '.error.code=="invalid-options"' "$work/error" >/dev/null
    for field in operation_id plan_sha256; do
        jq --arg field "$field" '.[$field]={bad:"type"}' "$plan" > "$work/forged"
        expect_error invalid-restore-plan restore stream-backup-plan "$work/forged" --output "$work/rejected"
    done
    # Use the same test under QEMU; the companion always remains host Bash.
    args=(--host-cli "$binary" --source-cli "$binary" --plan "$plan" --source-plan "$plan" --source-before-plan "$work/before-plan-$sector" --source-journal "$work/journal-$sector" --before "$work/before-$sector" --after "$work/after-$sector" --image "$image" --sector-size "$sector" --confirm "$confirmation" --local)
    bash "$component/scripts/restore-from-host.sh" --start "${args[@]}" | jq -e '.data.state=="COMMITTED" and .data.verified' >/dev/null
    [[ $desired == "$(sha256sum "$image" | cut -d' ' -f1)" ]]
    "$binary" restore stream-status "$work/journal-$sector" --image "$image" --sector-size "$sector" | jq -e '.data.classification=="TARGET" and .data.host_backup_trust=="HOST_ATTESTED" and (.data.device_verified_host_persistence==false)' >/dev/null
    bash "$component/scripts/restore-from-host.sh" --rollback "${args[@]}" | jq -e '.data.state=="ROLLED_BACK" and .data.verified' >/dev/null
    [[ $original == "$(sha256sum "$image" | cut -d' ' -f1)" ]]
    [[ ! -d $work/journal-$sector/before && ! -d $work/journal-$sector/after ]]
done
mkdir -- "$work/mock"
cat > "$work/mock/ssh" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
[[ $1 == -T && $2 == -o && $3 == BatchMode=yes && $4 == -o && $5 == StrictHostKeyChecking=yes && $6 == -- && $7 == fixture-host && $# == 8 ]]
remote=$8
if [[ ${URE_MOCK_INTERRUPT:-0} == 1 && $remote == *"'stream-chunk'"* && $remote == *"'--chunk' '1'"* ]]; then exit 77; fi
exec /bin/sh -c "$remote"
MOCK
cat > "$work/mock/adb" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
[[ $1 == -s && $2 == fixture-serial ]]; shift 2
if [[ $1 == features ]]; then [[ $# == 1 ]]; printf 'shell_v2\n'; exit; fi
[[ $1 == shell && $2 == -T && $3 == -e && $4 == none && $# == 5 ]]
exec /bin/sh -c "$5"
MOCK
chmod 700 -- "$work/mock/ssh" "$work/mock/adb"
for transport in ssh adb; do
    image="$work/$transport image ' literal "'$(touch NEVER_STREAM_RESTORE)'
    dd if=/dev/zero of="$image" bs=65536 count=3 status=none
    printf 'Desired transport bytes\n' | dd of="$image" conv=notrunc status=none
    printf 'Second desired chunk\n' | dd of="$image" bs=65536 seek=1 conv=notrunc status=none
    desired=$(sha256sum "$image" | cut -d' ' -f1)
    "$binary" backup storage-plan --image "$image" --profile fixture --chunk-size 65536 --output "$work/after-plan-$transport" >/dev/null
    "$binary" backup capture "$work/after-plan-$transport" --journal "$work/after-$transport" >/dev/null
    printf 'Original transport bytes\n' | dd of="$image" conv=notrunc status=none
    printf 'Second original chunk\n' | dd of="$image" bs=65536 seek=1 conv=notrunc status=none
    original=$(sha256sum "$image" | cut -d' ' -f1)
    "$binary" restore stream-plan "$work/after-plan-$transport" --image "$image" --profile fixture --output "$work/plan-$transport" >/dev/null
    confirmation=$(jq -er '.plan_sha256' "$work/plan-$transport")
    args=(--host-cli "$binary" --source-cli "$binary" --plan "$work/plan-$transport" --source-plan "$work/plan-$transport" --source-before-plan "$work/before-plan-$transport" --source-journal "$work/journal-$transport" --before "$work/before-$transport" --after "$work/after-$transport" --image "$image" --confirm "$confirmation")
    if [[ $transport == ssh ]]; then args+=(--ssh fixture-host)
    else args+=(--adb-serial fixture-serial); fi
    if [[ $transport == ssh ]]; then
        if PATH="$work/mock:$PATH" URE_MOCK_INTERRUPT=1 bash "$component/scripts/restore-from-host.sh" --start "${args[@]}" > "$work/interrupted"; then echo 'Mock disconnect did not interrupt restore' >&2; exit 1; fi
        "$binary" restore stream-status "$work/journal-$transport" --image "$image" | jq -e '.data.next_chunk==1 and .data.classification=="PARTIAL_EXPECTED_WRITE"' >/dev/null
        PATH="$work/mock:$PATH" bash "$component/scripts/restore-from-host.sh" --resume "${args[@]}" | jq -e '.data.state=="COMMITTED"' >/dev/null
    else PATH="$work/mock:$PATH" bash "$component/scripts/restore-from-host.sh" --start "${args[@]}" | jq -e '.data.state=="COMMITTED"' >/dev/null; fi
    [[ $desired == "$(sha256sum "$image" | cut -d' ' -f1)" ]]
    PATH="$work/mock:$PATH" bash "$component/scripts/restore-from-host.sh" --rollback "${args[@]}" | jq -e '.data.state=="ROLLED_BACK"' >/dev/null
    [[ $original == "$(sha256sum "$image" | cut -d' ' -f1)" ]]
done
[[ ! -e NEVER_STREAM_RESTORE ]]
printf 'Stream restore CLI, full host backups, explicit resume/rollback, strict SSH and serial-bound duplex ADB mocks passed; synthetic images only.\n'
