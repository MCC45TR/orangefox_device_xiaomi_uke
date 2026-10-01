#!/usr/bin/env bash
# Host-only duplex transport for reviewed raw-image stream restore journals.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
host_cli="$component/build/ure-host/uke-recoveryctl"
source_cli=uke-recoveryctl
transport=local mode= plan= source_plan= source_before_plan= source_journal= before= after= confirmation=
image= object= sector=4096 source_system_root= ssh_target= adb_serial=
declare -A seen=()
while (($#)); do
    key=$1
    [[ ! ${seen[$key]+present} ]] || { echo "Duplicate option: $key" >&2; exit 2; }
    seen[$key]=1
    case $key in
        --host-cli) host_cli=${2:?}; shift 2 ;;
        --source-cli) source_cli=${2:?}; shift 2 ;;
        --plan) plan=${2:?}; shift 2 ;;
        --source-plan) source_plan=${2:?}; shift 2 ;;
        --source-before-plan) source_before_plan=${2:?}; shift 2 ;;
        --source-journal) source_journal=${2:?}; shift 2 ;;
        --before) before=${2:?}; shift 2 ;;
        --after) after=${2:?}; shift 2 ;;
        --confirm) confirmation=${2:?}; shift 2 ;;
        --image) image=${2:?}; shift 2 ;;
        --object) object=${2:?}; shift 2 ;;
        --sector-size) sector=${2:?}; shift 2 ;;
        --source-system-root) source_system_root=${2:?}; shift 2 ;;
        --start|--resume|--rollback) [[ -z $mode ]]; mode=${key#--}; shift ;;
        --local) [[ $transport == local && -z $ssh_target && -z $adb_serial ]]; shift ;;
        --ssh) [[ $transport == local ]]; transport=ssh; ssh_target=${2:?}; shift 2 ;;
        --adb-serial) [[ $transport == local ]]; transport=adb; adb_serial=${2:?}; shift 2 ;;
        *) echo 'Use --start|--resume|--rollback --plan PLAN --source-plan PLAN --source-before-plan FILE --source-journal DIR --before HOST_STORE --after HOST_STORE --image IMAGE|--object ID --confirm SHA256 [--local|--ssh HOST|--adb-serial SERIAL].' >&2; exit 2 ;;
    esac
done
[[ -n $mode && -n $plan && -n $before && -n $after && -x $host_cli && -f $plan && ! -L $plan ]]
[[ $source_plan == /* && $source_before_plan == /* && $source_journal == /* && $source_cli != -* ]]
[[ $confirmation =~ ^[0-9a-f]{64}$ && $confirmation == "$(jq -er '.plan_sha256' "$plan")" ]]
target=()
if [[ -n $image ]]; then
    [[ $image == /* && -z $object && ( $sector == 512 || $sector == 4096 ) ]]; target=(--image "$image" --sector-size "$sector")
else
    [[ -n $object && ! ${seen[--sector-size]+present} ]]; target=(--object "$object")
fi
context=()
if [[ -n $source_system_root ]]; then [[ $source_system_root == /* ]]; context=(--system-root "$source_system_root"); fi
if [[ $transport == adb ]]; then
    [[ -n $adb_serial && $adb_serial != -* ]]
    adb -s "$adb_serial" features | awk '$0=="shell_v2" {found=1} END {exit !found}'
elif [[ $transport == ssh ]]; then [[ -n $ssh_target && $ssh_target != -* ]]; fi
quote_remote() { local escaped=${1//\'/\'\\\'\'}; printf "'%s'" "$escaped"; }
source_command() {
    local -a command=("$source_cli" "$@")
    if [[ $transport == local ]]; then "${command[@]}"; return; fi
    local remote= argument
    for argument in "${command[@]}"; do remote+="$(quote_remote "$argument") "; done
    if [[ $transport == ssh ]]; then ssh -T -o BatchMode=yes -o StrictHostKeyChecking=yes -- "$ssh_target" "$remote"
    else adb -s "$adb_serial" shell -T -e none "$remote"; fi
}
check_host_roots() {
    local name directory expected
    for name in before after; do
        if [[ $name == before ]]; then directory=$before; else directory=$after; fi
        [[ -d $directory && ! -L $directory ]]
        expected=$(jq -er --arg name "$name" '.[$name].root_identity | "\(.device) \(.inode)"' "$receipt")
        [[ $(stat -c '%d %i' -- "$directory") == "$expected" ]] || {
            echo 'A host backup directory was replaced; stop and review its identity.' >&2; return 1;
        }
    done
}
work=$(mktemp -d)
trap 'find "$work" -depth -mindepth 1 -delete; rmdir -- "$work"' EXIT
jq -e '.before' "$plan" > "$work/before.json"
# Hold the desired store for the whole session. Native verification additionally
# checks private ownership, sealed manifests and every chunk before attesting.
[[ -d $after && ! -L $after && -f $after/.lock && ! -L $after/.lock ]]
exec {after_lock}< "$after/.lock"
flock -sn "$after_lock"
receipt="$before/restore-host-receipt.json"
if [[ $mode == start ]]; then
    source_command restore stream-backup-plan "$source_plan" --output "$source_before_plan" > "$work/source-before.json"
    jq -e '.data' "$work/source-before.json" > "$work/source-manifest.json"
    jq -eS . "$work/source-manifest.json" > "$work/source-canonical.json"
    jq -eS . "$work/before.json" > "$work/host-canonical.json"
    cmp -- "$work/source-canonical.json" "$work/host-canonical.json"
    receiver=(--host-cli "$host_cli" --source-cli "$source_cli" --source-plan "$source_before_plan" --manifest "$work/before.json" --output "$before")
    [[ -z $source_system_root ]] || receiver+=(--source-system-root "$source_system_root")
    case $transport in local) receiver+=(--local);; ssh) receiver+=(--ssh "$ssh_target");; adb) receiver+=(--adb-serial "$adb_serial");; esac
    bash "$component/scripts/receive-backup.sh" "${receiver[@]}" > "$work/capture.json"
fi
[[ -d $before && ! -L $before && -f $before/.lock && ! -L $before/.lock ]]
exec {before_lock}< "$before/.lock"
flock -sn "$before_lock"
"$host_cli" restore host-receipt "$plan" --before "$before" --after "$after" --output "$work/receipt.json" > "$work/verified.json"
if [[ $mode == start ]]; then
    [[ ! -e $receipt && ! -L $receipt ]]
    (set -o noclobber; cat -- "$work/receipt.json" > "$receipt")
    sync -f "$receipt"
    source_command restore stream-begin "$source_plan" --receipt - "${target[@]}" "${context[@]}" --journal "$source_journal" --confirm "$confirmation" < "$receipt" > "$work/begin.json"
else
    [[ -f $receipt && ! -L $receipt ]]
    # Reconnect must retain the original host-directory identities. A replacement
    # store requires a newly reviewed plan rather than silently rebinding it.
    jq -eS '{before,after,plan_sha256,operation_id,trust,device_verified_host_persistence}' "$receipt" > "$work/old-binding.json"
    jq -eS '{before,after,plan_sha256,operation_id,trust,device_verified_host_persistence}' "$work/receipt.json" > "$work/new-binding.json"
    cmp -- "$work/old-binding.json" "$work/new-binding.json"
fi
source_command restore stream-status "$source_journal" "${target[@]}" "${context[@]}" > "$work/status.json"
[[ $(jq -er '.data.plan_sha256' "$work/status.json") == "$confirmation" &&
   $(jq -er '.data.receipt_sha256' "$work/status.json") == "$(jq -er '.receipt_sha256' "$receipt")" ]]
if [[ $mode == rollback ]]; then
    if [[ $(jq -er '.data.state' "$work/status.json") != ROLLED_BACK ]]; then
        source_command restore stream-rollback "$source_journal" "${target[@]}" "${context[@]}" --confirm "$confirmation" > "$work/status.json"
    fi
else [[ $(jq -er '.data.direction' "$work/status.json") == restore ]]; fi
while true; do
    check_host_roots
    phase=$(jq -er '.data.state' "$work/status.json")
    if [[ $phase == COMMITTED || $phase == ROLLED_BACK ]]; then
        [[ ( $phase == COMMITTED && $mode != rollback ) || ( $phase == ROLLED_BACK && $mode == rollback ) ]]
        if [[ $phase == COMMITTED ]]; then wanted=after; direction=restore; else wanted=before; direction=rollback; fi
        [[ $(jq -er '.data.direction' "$work/status.json") == "$direction" &&
           $(jq -er '.data.current_sha256' "$work/status.json") == "$(jq -er --arg wanted "$wanted" '.[$wanted].sha256' "$plan")" ]] || {
            echo 'Terminal journal differs from full current readback; stop and inspect the target.' >&2; exit 1;
        }
        cat -- "$work/status.json"; exit 0
    fi
    jq -e '.data.recovery_actions | index("host-resume") != null' "$work/status.json" > /dev/null
    index=$(jq -er '.data.next_chunk' "$work/status.json")
    count=$(jq -er '.data.chunk_count' "$work/status.json")
    if ((index==count)); then break; fi
    # Both exports are hash-checked again. The target receives precisely original
    # then desired bytes, with EOF and a remote exit status on the same session.
    {
        "$host_cli" backup store-export "$before" --chunk "$index" || exit 1
        "$host_cli" backup store-export "$after" --chunk "$index" || exit 1
    } | source_command restore stream-chunk "$source_journal" "${target[@]}" "${context[@]}" --chunk "$index" --confirm "$confirmation" > "$work/status.json"
    printf 'Verified restore chunk %s/%s\n' "$((index+1))" "$count" >&2
done
source_command restore stream-finish "$source_journal" "${target[@]}" "${context[@]}" --confirm "$confirmation"
