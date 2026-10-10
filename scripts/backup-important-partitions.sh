#!/usr/bin/env bash
# Host-only orchestration of the existing native read-only backup contract.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mode=list serial= profile= destination= source_cli=/system/bin/uke-recoveryctl
host_cli="$component/build/ure-host/uke-recoveryctl"
partitions=()
declare -A seen=()
usage() {
    echo 'Usage: backup-important-partitions.sh [--list|--capture|--resume DIR|--verify DIR] [--adb-serial SERIAL] [--profile PROFILE] [--partition LABEL ...] [--output NEW_DIR] [--host-cli CLI] [--source-cli CLI]' >&2
}
while (($#)); do
    key=$1
    if [[ $key != --partition ]]; then
        [[ ! ${seen[$key]+present} ]] || { usage; exit 2; }
        seen[$key]=1
    fi
    case $key in
        --list|--capture) [[ ! ${seen[mode]+present} ]]; seen[mode]=1; mode=${key#--}; shift ;;
        --resume|--verify) [[ ! ${seen[mode]+present} ]]; seen[mode]=1; mode=${key#--}; destination=${2:?}; shift 2 ;;
        --adb-serial) serial=${2:?}; shift 2 ;;
        --profile) profile=${2:?}; shift 2 ;;
        --partition) [[ ${2:?} =~ ^[a-zA-Z0-9_][a-zA-Z0-9_.-]{0,63}$ ]]; partitions+=("$2"); shift 2 ;;
        --output) destination=${2:?}; shift 2 ;;
        --host-cli) host_cli=${2:?}; shift 2 ;;
        --source-cli) source_cli=${2:?}; shift 2 ;;
        --help) usage; exit 0 ;;
        *) usage; exit 2 ;;
    esac
done
[[ $source_cli == /* && ${#source_cli} -le 512 && $source_cli != *$'\n'* ]]
if [[ $mode == list || $mode == capture ]]; then
    [[ ! ${seen[--output]+present} || $mode == capture ]]
else
    [[ ! ${seen[--output]+present} && ! ${seen[--profile]+present} && ${#partitions[@]} == 0 ]]
fi
if [[ $mode != verify ]]; then
    [[ -n $serial && $serial != -* && ${#serial} -le 256 && $serial != *$'\n'* ]]
    timeout 30 adb -s "$serial" features | awk '$0=="shell_v2" {found=1} END {exit !found}'
fi
quote_remote() { local escaped=${1//\'/\'\\\'\'}; printf "'%s'" "$escaped"; }
remote() {
    local command= argument
    for argument in "$source_cli" "$@"; do command+="$(quote_remote "$argument") "; done
    # shell_v2 keeps binary stdout, stderr and the remote exit status separate.
    timeout --foreground 12h adb -s "$serial" shell -T -e none "$command"
}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
if [[ $mode == list || $mode == capture ]]; then
    remote storage inventory > "$work/inventory.json"
    requested=$(printf '%s\n' "${partitions[@]}" | jq -Rsc 'split("\n") | map(select(length>0))')
    jq -e --argjson requested "$requested" '
      .result=="ok" and (.data.objects|type=="array")' "$work/inventory.json" >/dev/null
    jq --argjson requested "$requested" '
      [.data.objects[] | select(.partition==true) | select(
        if ($requested|length)>0 then .label as $label | $requested|index($label)
        else .label | test("^(boot|init_boot|vendor_boot|recovery|dtbo|vbmeta|vbmeta_system|bluetooth|dsp|modem)_[ab]$|^(metadata|userdata|persist|misc|frp|super|fsg|fsc|modemst1|modemst2)$") end)
        | {label,stable_id,bytes}] | sort_by(.label)' "$work/inventory.json" > "$work/selection.json"
    jq -e --argjson requested "$requested" '
      length>0 and length<=128 and
      all(.[]; (.label|type=="string" and test("^[a-zA-Z0-9_][a-zA-Z0-9_.-]{0,63}$")) and
        (.stable_id|type=="string" and length<=512 and test("^(partuuid:|sysfs:)")) and
        (.bytes|type=="number" and .>0 and .<=1099511627776 and floor==.)) and
      (map(.label)|unique|length)==length and (map(.stable_id)|unique|length)==length and
      (. as $rows | all($requested[]; . as $label | any($rows[]; .label==$label)))' "$work/selection.json" >/dev/null
    if [[ $mode == list ]]; then
        jq '{partitions:.,total_bytes:(map(.bytes)|add),source_read_only:true,restore_authorized:false,physical_test_record:false}' "$work/selection.json"
        exit 0
    fi
    [[ $profile =~ ^[a-zA-Z0-9_][a-zA-Z0-9_.-]{0,127}$ && -x $host_cli ]]
    if [[ -z $destination ]]; then
        mkdir -p -- "$component/reports/private/partition-backups"
        destination=$(mktemp -d "$component/reports/private/partition-backups/$(date -u +%Y%m%dT%H%M%SZ)-XXXXXXXX")
    else
        [[ ! -e $destination && ! -L $destination ]]
        mkdir -m 700 -- "$destination"
    fi
    run_id="$(date -u +%Y%m%dT%H%M%SZ)-${BASHPID}-${RANDOM}${RANDOM}"
    jq --arg profile "$profile" --arg run "$run_id" '
      {schema:1,format:"ure-host-partition-backup",state:"PARTIAL",firmware_profile:$profile,
       source_read_only:true,restore_authorized:false,atomic_snapshot:false,private_record:true,physical_test_record:false,
       partitions:map(.+{source_plan:("/tmp/ure-host-backup-"+$run+"-"+.label+".json")})}' \
        "$work/selection.json" > "$destination/manifest.json"
    cp -- "$work/inventory.json" "$destination/inventory.json"
    mkdir -m 700 -- "$destination/plans" "$destination/partitions"
fi
[[ -d $destination && ! -L $destination && -x $host_cli ]]
[[ $(stat -c '%u:%a' -- "$destination") == "$(id -u):700" ]]
destination=$(realpath -e -- "$destination")
[[ ! -L $destination/.lock ]]
exec {backup_lock}>> "$destination/.lock"
flock -n "$backup_lock"
manifest="$destination/manifest.json"
[[ -f $manifest && ! -L $manifest && $(stat -c '%u:%a:%h' -- "$manifest") == "$(id -u):600:1" ]]
jq -e '.schema==1 and .format=="ure-host-partition-backup" and .source_read_only==true and
  .restore_authorized==false and .atomic_snapshot==false and .private_record==true and
  (.partitions|type=="array" and length>0 and length<=128) and
  all(.partitions[]; (.label|type=="string" and test("^[a-zA-Z0-9_][a-zA-Z0-9_.-]{0,63}$")) and
    (.stable_id|type=="string" and length<=512 and test("^(partuuid:|sysfs:)")) and
    (.bytes|type=="number" and .>0 and .<=1099511627776 and floor==.) and
    (.source_plan|type=="string" and test("^/tmp/ure-host-backup-[a-zA-Z0-9_.-]{1,128}\\.json$"))) and
  ((.partitions|map(.label)|unique|length)==(.partitions|length)) and
  ((.partitions|map(.stable_id)|unique|length)==(.partitions|length))' "$manifest" >/dev/null
[[ -d $destination/plans && ! -L $destination/plans && -d $destination/partitions && ! -L $destination/partitions ]]
profile=$(jq -er '.firmware_profile' "$manifest")
failed=0
while IFS=$'\t' read -r label object bytes source_plan; do
    plan="$destination/plans/$label.json"
    store="$destination/partitions/$label"
    if [[ $mode != verify && ! -e $plan ]]; then
        printf 'Planning read-only source %s (%s bytes)\n' "$label" "$bytes" >&2
        if ! remote backup storage-plan --object "$object" --profile "$profile" --chunk-size 67108864 \
            --output "$source_plan" > "$work/result.json" 2> "$destination/plans/$label.stderr"; then
            echo "Native planning refused or disconnected for $label; inspect its private stderr file." >&2
            failed=1; continue
        fi
        jq -e '.result=="ok" and (.data|type=="object")' "$work/result.json" >/dev/null
        temporary=$(mktemp "$destination/plans/.incomplete-plan-XXXXXXXX")
        jq '.data' "$work/result.json" > "$temporary"
        # Native publication refuses replacement; interruption never truncates a plan.
        ln -- "$temporary" "$plan"
        unlink -- "$temporary"
        sync -f "$plan"
    fi
    if [[ ! -f $plan || -L $plan ]] || ! jq -e --arg label "$label" --arg object "$object" --argjson bytes "$bytes" '
        .schema==2 and .source_kind=="live-block" and .source_identity.kind=="live-block" and
        .source_identity.label==$label and .source_identity.stable_id==$object and .source_identity.bytes==$bytes and
        .restore_authorized==false and .atomic_snapshot==false' "$plan" >/dev/null; then
        echo "Missing or mismatched native plan for $label." >&2; failed=1; continue
    fi
    if [[ $mode != verify ]]; then
        if ! timeout --foreground 12h bash "$component/scripts/receive-backup.sh" --manifest "$plan" --output "$store" \
            --source-plan "$source_plan" --source-cli "$source_cli" --host-cli "$host_cli" --adb-serial "$serial" \
            > "$work/verification.json"; then
            echo "Capture incomplete for $label; verified chunks are retained for same-boot resume." >&2
            failed=1; continue
        fi
    elif ! "$host_cli" backup verify "$store" > "$work/verification.json"; then
        echo "Native offline verification refused $label." >&2; failed=1; continue
    fi
    if ! jq -e '.result=="ok" and .data.verified==true and .data.state=="COMPLETE"' "$work/verification.json" >/dev/null; then
        echo "Backup remains partial for $label." >&2; failed=1; continue
    fi
    temporary=$(mktemp "$destination/.incomplete-manifest-XXXXXXXX")
    jq --arg label "$label" --slurpfile plan "$plan" '
      (.partitions[]|select(.label==$label)) += {bytes:$plan[0].source_identity.bytes,
        sha256:$plan[0].sha256,plan_sha256:$plan[0].plan_sha256,verified:true}' "$manifest" > "$temporary"
    mv -- "$temporary" "$manifest"
done < <(jq -r '.partitions[]|[.label,.stable_id,.bytes,.source_plan]|@tsv' "$manifest")
state=PARTIAL
if ((failed==0)); then state=COMPLETE; fi
temporary=$(mktemp "$destination/.incomplete-manifest-XXXXXXXX")
jq --arg state "$state" '.state=$state | .physical_test_record=false' "$manifest" > "$temporary"
mv -- "$temporary" "$manifest"
sync -f "$manifest"
printf 'Host backup %s: %s\n' "$state" "$destination" >&2
exit "$failed"
