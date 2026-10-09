#!/usr/bin/env bash
# Production build receipts. Never attest an arbitrary old output directory.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
android="$component/src/upstream/orangefox-android16"
build_scripts="$component/scripts"
source "$build_scripts/build-evidence-lib.sh"
source "$build_scripts/release-policy-lib.sh"
mode=${1:?begin|seal|publish|acknowledge|verify|payload|export}
shift
if [[ $mode == export ]]; then
    [[ $# == 1 ]]
    release_output_mutable "$component" "$1"
fi
if [[ ( $mode == verify || $mode == export ) && ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" native bash "$component/scripts/build-evidence.sh" "$mode" "$@"
fi
build_publisher="$component/build/host-tools/publish-build-directory"
prepare_publisher() {
    mkdir -p "$component/build/host-tools" "$component/build/ccache/native"
    CCACHE_DIR="$component/build/ccache/native" CCACHE_COMPILERCHECK=content CCACHE_SLOPPINESS= \
        ccache /usr/bin/c++ -std=c++20 -O2 -Wall -Wextra -Werror "$component/src/host/publish-build-directory.cpp" -o "$build_publisher"
}
job_path() {
    [[ $1 =~ ^job-[a-zA-Z0-9]{12}$ ]]
    job="$component/build/android-builds/$1"
    [[ -d $job && ! -L $job && $(stat -c '%u %a' "$job") == "$UID 700" ]]
}
current_job() {
    local output="$android/out-public" id
    [[ -d $output && ! -L $output && -f $output/.uke-build-id && ! -L $output/.uke-build-id ]]
    id=$(cat "$output/.uke-build-id")
    job_path "$id"
}
case $mode in
    begin)
        [[ ${UKE_HOST_BUDGET_ACTIVE:-0} == 1 && ( $# == 2 || $# == 3 ) ]]
        jq -e '.mode=="arm64" and .admitted' /tmp/admitted-policy.json >/dev/null
        prepare_publisher
        for kind in soong blueprint; do bash "$build_scripts/prepare-reviewed-patches.sh" "$android/build/$kind" check "$kind"; done
        bash "$build_scripts/prepare-reviewed-patches.sh" "$android/bootable/recovery" check recovery
        bash "$build_scripts/prepare-reviewed-patches.sh" "$android/system/core" check fastboot
        bash "$build_scripts/prepare-reviewed-patches.sh" "$android/system/vold" check vold
        mkdir -p "$component/build/android-builds"
        chmod 0700 "$component/build/android-builds"
        # Reserve an unguessable job name without exposing an old output to begin.
        reserved=$(mktemp -d "$component/build/android-builds/job-XXXXXXXXXXXX")
        rmdir "$reserved"
        seed=${3:-fresh}
        [[ $seed == fresh || $seed =~ ^job-[a-zA-Z0-9]{12}$ ]]
        if [[ $seed != fresh ]]; then seed="$component/build/android-builds/$seed"; else seed=; fi
        build_begin "$component" "$android" "$reserved" android-recovery 399 "$1" "$2" "$seed"
        cp -- /tmp/admitted-policy.json /tmp/host-temp-admitted.json "$reserved/"
        printf '%s\n' "${reserved##*/}"
        ;;
    seal)
        [[ ${UKE_HOST_BUDGET_ACTIVE:-0} == 1 && $# == 1 ]]
        job_path "$1"
        build_seal "$component" "$android" "$job"
        jq -cn --arg job "$1" '{schema_version:1,job_id:$job}' > /tmp/android-build-pending.json
        ;;
    publish)
        [[ ${UKE_HOST_BUDGET_ACTIVE:-0} == 1 && $# == 1 && $(cat /tmp/command-status) == 0 ]]
        jq -e --arg job "$1" '.schema_version==1 and .job_id==$job' /tmp/android-build-pending.json >/dev/null
        job_path "$1"
        # The service, rather than its build child, records zero OOM deltas.
        awk 'NR==FNR {b[$1]=$2; next} ($1=="oom" || $1=="oom_kill" || $1=="oom_group_kill") && $2!=b[$1] {exit 1}' \
            /tmp/memory.events.before /tmp/memory.events.after
        [[ -x $build_publisher && ! -L $build_publisher ]]
        build_publish "$job" "$android/out-public" android-recovery
        jq -cn --arg receipt "$(build_digest "$job/receipt/SHA256SUMS")" \
            --arg policy "$(build_digest /tmp/admitted-policy.json)" --arg temp "$(build_digest /tmp/host-temp-admitted.json)" \
            '{schema_version:1,receipt_index_sha256:$receipt,host_policy_sha256:$policy,host_temp_policy_sha256:$temp,
              command_status:0,new_oom_events:0,published:true}' > "$job/.service-incomplete.json"
        sync -f "$job/.service-incomplete.json"
        chmod 0444 "$job/.service-incomplete.json"
        mv -- "$job/.service-incomplete.json" "$job/SERVICE-PUBLICATION.json"
        sync -f "$job/SERVICE-PUBLICATION.json"
        ;;
    acknowledge)
        [[ $# == 1 && ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]
        scratch=$(realpath -e -- "$1")
        [[ $scratch == "$component/build/host-budget/arm64-"* && ! -L $1 && $(stat -c '%u %a' "$scratch") == "$UID 700" ]]
        [[ $(cat "$scratch/command-status") == 0 ]]
        id=$(jq -er '.job_id' "$scratch/android-build-pending.json")
        job_path "$id"
        [[ ! -e $job/SERVICE-COMPLETION.json && -f $job/SERVICE-PUBLICATION.json && ! -L $job/SERVICE-PUBLICATION.json ]]
        jq -e --arg receipt "$(build_digest "$job/receipt/SHA256SUMS")" \
            '.schema_version==1 and .receipt_index_sha256==$receipt and .command_status==0 and .new_oom_events==0 and .published' \
            "$job/SERVICE-PUBLICATION.json" >/dev/null
        # Only the outer controller calls here, after systemd-run returned zero.
        # A late worker kill or a lost controller can never self-acknowledge.
        jq '. + {systemd_run_status:0,controller_confirmed:true}' "$job/SERVICE-PUBLICATION.json" > "$job/.controller-incomplete.json"
        sync -f "$job/.controller-incomplete.json"
        chmod 0444 "$job/.controller-incomplete.json"
        mv -- "$job/.controller-incomplete.json" "$job/SERVICE-COMPLETION.json"
        sync -f "$job/SERVICE-COMPLETION.json"
        ;;
    verify)
        [[ $# == 0 ]]
        current_job
        work=$(mktemp -d "$component/build/build-verification-XXXXXXXX")
        trap 'rm -rf -- "$work"' EXIT
        build_verify "$component" "$android" "$job" "$android/out-public" android-recovery "$work"
        printf '%s\n' 'Current build inputs, service completion and output content match the sealed receipt.'
        ;;
    payload)
        [[ $# == 3 ]]
        release_output_mutable "$component" "$3"
        current_job
        build_receipt_integrity "$job" android-recovery
        [[ -s $job/SERVICE-COMPLETION.json ]]
        jq -e --arg receipt "$(build_digest "$job/receipt/SHA256SUMS")" \
            '.receipt_index_sha256==$receipt and .command_status==0 and .new_oom_events==0 and .published and
             .systemd_run_status==0 and .controller_confirmed' "$job/SERVICE-COMPLETION.json" >/dev/null
        [[ $(build_digest "$1") == "$(jq -er .recovery_image_sha256 "$job/receipt/BUILD-COMPLETION.json")" ]]
        work=$(mktemp -d "$component/build/payload-build-verification-XXXXXXXX")
        trap 'rm -rf -- "$work"' EXIT
        # Bind every input to the packaging conversion before replaying it.
        # mkbootfs deliberately changes staging modes through Android fs_config.
        bash "$build_scripts/index-build-tree.sh" "$android/out-public" "$work/products" products
        diff -qr -- "$job/receipt/products" "$work/products" >/dev/null
        bash "$build_scripts/check-packed-payload.sh" "$android/out-public" "$1" "$2" "$work/packed"
        jq -cn --arg receipt "$(build_digest "$job/receipt/SHA256SUMS")" \
            --arg completed "$(build_digest "$job/receipt/BUILD-COMPLETION.json")" \
            --arg service "$(build_digest "$job/SERVICE-COMPLETION.json")" \
            --arg image "$(build_digest "$1")" \
            '{schema_version:1,evidence_class:"android-recovery",receipt_index_sha256:$receipt,build_completion_sha256:$completed,
              service_completion_sha256:$service,recovery_image_sha256:$image,extracted_payload_matches:true,
              packaging_inputs_match:true,canonical_cpio_matches:true,canonical_payload_modes_and_links_match:true,physical_device:false}' > "$3"
        ;;
    export)
        [[ $# == 1 ]]
        current_job
        work=$(mktemp -d "$component/build/build-export-XXXXXXXX")
        trap 'rm -rf -- "$work"' EXIT
        build_verify "$component" "$android" "$job" "$android/out-public" android-recovery "$work"
        jq --slurpfile service "$job/SERVICE-COMPLETION.json" \
            --slurpfile localization "$job/receipt/inputs/localization.json" \
            --arg localization_sha "$(build_digest "$job/receipt/inputs/localization.json")" \
            --arg receipt "$(build_digest "$job/receipt/SHA256SUMS")" \
            --arg project_files "$(build_digest "$job/receipt/inputs/project/files.sha256")" \
            --arg android_files "$(build_digest "$job/receipt/inputs/android/files.sha256")" \
            --arg revisions "$(build_digest "$job/receipt/inputs/revisions.tsv")" \
            --arg tools "$(build_digest "$job/receipt/inputs/host-tools.tsv")" \
            --arg runtime "$(build_digest "$job/receipt/inputs/host-runtime.tsv")" \
            'del(.job_id,.output_identity,.output_origin.seed.producer_job_id,.output_origin.seed.producer_output_identity) | .receipt_index_sha256=$receipt |
             .inputs={project_file_manifest_sha256:$project_files,android_file_manifest_sha256:$android_files,
                      android_revisions_sha256:$revisions,host_executable_manifest_sha256:$tools,host_direct_runtime_manifest_sha256:$runtime,
                      localization:$localization[0],localization_inputs_sha256:$localization_sha} |
             .validation.service_completed=($service[0].command_status==0 and $service[0].new_oom_events==0 and $service[0].published and
                                          $service[0].systemd_run_status==0 and $service[0].controller_confirmed) |
             .validation.current_source_and_output_match=true | .validation.full_host_os_closure=false |
             .state="service-completed"' "$job/receipt/BUILD-COMPLETION.json" > "$1"
        ;;
    *) exit 2;;
esac
