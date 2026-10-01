#!/usr/bin/env bash
# Host-only receiver. Transport stdout contains binary chunks; no tablet interpreter.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
host_cli="$component/build/ure-host/uke-recoveryctl"
transport=local
source_cli=uke-recoveryctl
source_root= source_system_root= source_plan= ssh_target= manifest= destination=
while (($#)); do
    case $1 in
        --host-cli) host_cli=${2:?}; shift 2 ;;
        --source-cli) source_cli=${2:?}; shift 2 ;;
        --source-root) source_root=${2:?}; shift 2 ;;
        --source-system-root) source_system_root=${2:?}; shift 2 ;;
        --source-plan) source_plan=${2:?}; shift 2 ;;
        --manifest) manifest=${2:?}; shift 2 ;;
        --output) destination=${2:?}; shift 2 ;;
        --local) transport=local; shift ;;
        --adb) transport=adb; shift ;;
        --ssh) transport=ssh; ssh_target=${2:?}; shift 2 ;;
        *) echo 'Usage: receive-backup.sh --manifest PLAN --output DIR --source-plan PLAN [--source-root ROOT for file plans] [--source-system-root ROOT for storage plans] [--local|--adb|--ssh HOST] [--host-cli CLI] [--source-cli CLI]' >&2; exit 2 ;;
    esac
done
[[ -n $manifest && -n $destination && -n $source_plan && -x $host_cli && -f $manifest && ! -L $manifest ]]
[[ $source_plan == /* && $source_cli != -* ]]
source_kind=$(jq -er '.source_kind' "$manifest")
source_context=()
case $source_kind in
    regular-file) [[ $source_root == /* && -z $source_system_root ]]; source_context=(--root "$source_root") ;;
    storage-image|live-block) [[ -z $source_root ]];
        if [[ -n $source_system_root ]]; then [[ $source_system_root == /* ]]; source_context=(--system-root "$source_system_root"); fi ;;
    *) echo 'Unsupported backup source kind.' >&2; exit 2 ;;
esac
if [[ ! -e $destination ]]; then
    mkdir -m 700 -- "$destination"
    # Exclusive output creation is a recovery boundary; interrupted setup requires
    # inspecting the directory instead of silently replacing its manifest.
    (set -o noclobber; cat -- "$manifest" > "$destination/plan.json")
    chmod 600 -- "$destination/plan.json"
else
    [[ -d $destination && ! -L $destination ]]
    cmp -- "$manifest" "$destination/plan.json"
fi
# The native verifier checks the sealed manifest, ownership, modes, chunk sizes,
# hashes and gaps before the host accepts any new data.
[[ ! -L $destination/.lock ]]
exec {receiver_lock}>> "$destination/.lock"
flock -n "$receiver_lock"
preflight=$(mktemp)
trap 'unlink -- "$preflight"' EXIT
"$host_cli" backup verify "$destination" > "$preflight"
quote_remote() { local escaped=${1//\'/\'\\\'\'}; printf "'%s'" "$escaped"; }
receive_chunk() {
    local index=$1
    local -a command=("$source_cli" backup export "$source_plan" "${source_context[@]}" --chunk "$index")
    case $transport in
        local) "${command[@]}" ;;
        adb|ssh)
            # adb/SSH pass a remote command through a POSIX shell. Quote every
            # argument independently; never evaluate manifest text as code.
            local remote= argument
            for argument in "${command[@]}"; do remote+="$(quote_remote "$argument") "; done
            if [[ $transport == adb ]]; then adb exec-out "$remote"
            else ssh -T -o BatchMode=yes -o StrictHostKeyChecking=yes -- "$ssh_target" "$remote"; fi
            ;;
    esac
}
count=$(jq -er '.chunks | length' "$manifest")
start=$(jq -er '.data.next_chunk' "$preflight")
for ((index=start;index<count;index++)); do
    printf -v name 'chunk-%05d.bin' "$index"
    temporary=$(mktemp "$destination/.incomplete-host-XXXXXXXX")
    if ! receive_chunk "$index" > "$temporary"; then
        echo "Transfer interrupted at chunk $index; incomplete file retained for inspection." >&2
        exit 1
    fi
    expected=$(jq -er --argjson index "$index" '.chunks[$index].sha256' "$manifest")
    expected_bytes=$(jq -er --argjson index "$index" '.chunks[$index].bytes' "$manifest")
    actual=$(sha256sum -- "$temporary"); actual=${actual%% *}
    [[ $actual == "$expected" && $(stat -c %s -- "$temporary") == "$expected_bytes" ]] || {
        echo "Chunk $index failed verification; incomplete file retained." >&2; exit 1;
    }
    sync -f "$temporary"
    # Hard-link publication is exclusive and does not overwrite an existing chunk.
    ln -- "$temporary" "$destination/$name"
    unlink -- "$temporary"
    sync -f "$destination"
    printf 'Verified chunk %s/%s (%s bytes)\n' "$((index+1))" "$count" "$expected_bytes" >&2
    jq -n --argjson next "$((index+1))" --arg transport "$transport" \
        '{schema:1,next_chunk:$next,transport:$transport,private_record:true,physical_test_record:false}' > "$destination/receiver-progress.json"
done
"$host_cli" backup verify "$destination"
