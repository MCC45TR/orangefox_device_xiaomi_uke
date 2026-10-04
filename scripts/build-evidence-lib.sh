#!/usr/bin/env bash
# Shared host evidence primitives; production paths/class are set by the caller.
set -euo pipefail
export LC_ALL=C
umask 077
build_digest() { sha256sum -- "$1" | cut -d' ' -f1; }
build_identity() { stat -c '%d:%i:%u:%a' -- "$1"; }
build_pins() {
    local component=$1 android=$2 destination=$3 expected_count=$4 line path name revision actual count=0
    local name_pattern='name="([^"]+)"' path_pattern='path="([^"]+)"' revision_pattern='revision="([0-9a-f]{40})"'
    xmllint --nonet --format "$component/manifests/orangefox-android16-uke.lock.xml" > "$destination/lock.xml"
    : > "$destination/revisions.tsv"
    : > "$destination/project-list"
    while IFS= read -r line; do
        [[ $line == *'<project '* ]] || continue
        [[ $line =~ $name_pattern ]]; name=${BASH_REMATCH[1]}
        path=$name
        if [[ $line =~ $path_pattern ]]; then path=${BASH_REMATCH[1]}; fi
        [[ $path =~ ^[a-zA-Z0-9._/-]+$ && $path != /* && $path != *'/../'* && $path != ../* && $path != */.. ]]
        [[ $line =~ $revision_pattern ]]; revision=${BASH_REMATCH[1]}
        actual=$(git -C "$android/$path" rev-parse HEAD)
        [[ $actual == "$revision" ]] || { echo 'An Android source revision differs from the locked build input.' >&2; return 1; }
        printf '%s\t%s\t%s\n' "$path" "$actual" "$(git -C "$android/$path" rev-parse HEAD^{tree})" >> "$destination/revisions.tsv"
        printf '%s\n' "$path" >> "$destination/project-list"
        count=$((count+1))
    done < "$destination/lock.xml"
    [[ $count == "$expected_count" ]]
    sort -o "$destination/revisions.tsv" "$destination/revisions.tsv"
    sort -o "$destination/project-list" "$destination/project-list"
    sort "$android/.repo/project.list" > "$destination/actual-project-list"
    cmp "$destination/project-list" "$destination/actual-project-list"
    rm -- "$destination/project-list" "$destination/actual-project-list" "$destination/lock.xml"
}
build_inputs() {
    local component=$1 android=$2 destination=$3 count=$4 command resolved tool runtime
    mkdir -m 0700 -- "$destination"
    bash "$build_scripts/index-build-tree.sh" "$component" "$destination/project" project
    bash "$build_scripts/index-build-tree.sh" "$android" "$destination/android" android
    build_pins "$component" "$android" "$destination" "$count"
    : > "$destination/host-tools.tsv"
    : > "$destination/host-runtime.tsv"
    # Upstream host Python is used by AOSP tooling only. Target payloads still
    # must pass the separate recursive no-Python audit. OS closure is not claimed.
    for command in bash awk bwrap ccache cp cpio c++ dd find git gzip jq ln make mv od perl python3 readelf sed sha256sum sort stat sync tar touch unzip xargs xmllint zip; do
        tool=$(command -v "$command")
        resolved=$(realpath -e -- "$tool")
        [[ -f $resolved && ! -L $resolved ]]
        printf '%s\t%s\n' "$command" "$(build_digest "$resolved")" >> "$destination/host-tools.tsv"
        # Installed trusted ELF host tools only; never ldd an input/target ELF.
        if [[ $(head -c 4 "$resolved" | od -An -tx1 | tr -d ' \n') == 7f454c46 ]]; then
            ldd "$resolved" > "$destination/ldd.tmp"
            ! rg -q 'not found' "$destination/ldd.tmp"
            while IFS= read -r runtime; do
                [[ -f $runtime ]]
                printf '%s\t%s\n' "${runtime##*/}" "$(build_digest "$(realpath -e -- "$runtime")")" >> "$destination/host-runtime.tsv"
            done < <(awk '{for(i=1;i<=NF;i++) if($i~/^\//) print $i}' "$destination/ldd.tmp")
        fi
    done
    rm -f -- "$destination/ldd.tmp"
    sort -u -o "$destination/host-runtime.tsv" "$destination/host-runtime.tsv"
    [[ ! -f /etc/os-release ]] || cp -- /etc/os-release "$destination/host-os-release"
    if command -v rpm >/dev/null; then rpm -qa --qf '%{NAME}\t%{EPOCHNUM}:%{VERSION}-%{RELEASE}.%{ARCH}\n' | sort > "$destination/host-package-versions.tsv"; fi
}
build_same_inputs() {
    diff -qr -- "$1" "$2" >/dev/null || { echo 'Build input content, modes, links, pins or host tools changed; no completion accepted.' >&2; return 1; }
}
build_begin() {
    local component=$1 android=$2 job=$3 class=$4 count=$5 jobs=$6 vm=$7
    [[ ! -e $job && $class =~ ^(android-recovery|host-fixture)$ && $jobs =~ ^([1-9]|1[0-6])$ && $vm =~ ^[01]$ ]]
    mkdir -m 0700 -- "$job"
    mkdir -m 0700 -- "$job/output"
    build_inputs "$component" "$android" "$job/inputs-before" "$count"
    jq -cn --arg class "$class" --arg identity "$(build_identity "$job/output")" --arg publisher "$(build_digest "$build_publisher")" --argjson count "$count" \
        --arg go_procs "${GOMAXPROCS:-unspecified}" --arg go_heap "${GOMEMLIMIT:-unspecified}" --arg go_gc "${GOGC:-unspecified}" \
        --argjson jobs "$jobs" --argjson vm "$vm" \
        '{schema_version:1,evidence_class:$class,state:"prepared",output_identity:$identity,publisher_sha256:$publisher,expected_project_count:$count,
          build:{lunch:"twrp_uke-bp2a-eng",targets:(if $vm==1 then ["recoveryimage","uke-btrfs-vm-fixture-soong"] else ["recoveryimage"] end),compile_jobs:$jobs},
          environment:{path:"/usr/bin:/bin",locale:"C",timezone:"UTC",home:"/tmp/uke-build-home",output:"/mnt/out-public",
            source_date_epoch:1790726400,build_datetime:1790726400,build_number:"uke-r12",host_python_write_bytecode:false,
            go_procs:$go_procs,go_heap:$go_heap,go_gc:$go_gc}}' \
        > "$job/prepared.json"
}
build_seal() {
    local component=$1 android=$2 job=$3 id=${3##*/} count identity stage binary payload file
    [[ -s $job/prepared.json && ! -e $job/receipt && ! -L $job/output ]]
    [[ $(build_digest "$build_publisher") == "$(jq -er .publisher_sha256 "$job/prepared.json")" ]]
    [[ $(build_identity "$job/output") == "$(jq -er .output_identity "$job/prepared.json")" ]]
    count=$(jq -er .expected_project_count "$job/prepared.json")
    build_inputs "$component" "$android" "$job/inputs-after" "$count"
    build_same_inputs "$job/inputs-before" "$job/inputs-after"
    payload="$job/output/target/product/uke/recovery/root"
    for binary in recovery fastbootd uke-recoveryctl uke-recovery-install; do
        file="$payload/system/bin/$binary"
        [[ -s $file && ! -L $file ]]
        readelf -h "$file" | rg 'Machine:.*AArch64' >/dev/null
        readelf -h "$file" | rg 'Class:.*ELF64' >/dev/null
    done
    [[ -s $job/output/target/product/uke/recovery.img && ! -L $job/output/target/product/uke/recovery.img ]]
    if jq -e '.build.targets|index("uke-btrfs-vm-fixture-soong")!=null' "$job/prepared.json" >/dev/null; then
        [[ -s $job/output/soong/.intermediates/device/xiaomi/uke/recoveryctl/uke-btrfs-vm-fixture/android_recovery_arm64_armv8-a/uke-btrfs-vm-fixture ]]
    fi
    stage="$job/.receipt-incomplete"
    [[ ! -e $stage ]]
    mkdir -m 0700 -- "$stage"
    cp -a -- "$job/inputs-before" "$stage/inputs"
    bash "$build_scripts/index-build-tree.sh" "$job/output" "$stage/products" products
    bash "$build_scripts/index-build-tree.sh" "$payload" "$stage/payload" payload
    cp -- "$job/prepared.json" "$stage/prepared.json"
    if [[ $(jq -er .evidence_class "$job/prepared.json") == android-recovery ]]; then
        [[ -s $job/admitted-policy.json && -s $job/host-temp-admitted.json ]]
        cp -- "$job/admitted-policy.json" "$job/host-temp-admitted.json" "$stage/"
    fi
    printf '%s\n' "$id" > "$job/output/.uke-build-id"
    jq -cn --arg id "$id" --arg image "$(build_digest "$job/output/target/product/uke/recovery.img")" \
        --slurpfile prepared "$job/prepared.json" \
        --arg products "$(build_digest "$stage/products/index.sha256")" --arg payload "$(build_digest "$stage/payload/index.sha256")" \
        '{schema_version:1,job_id:$id,evidence_class:$prepared[0].evidence_class,state:"compiled",build:$prepared[0].build,environment:$prepared[0].environment,
          output_identity:$prepared[0].output_identity,recovery_image_sha256:$image,products_index_sha256:$products,payload_index_sha256:$payload,
          validation:{command_completed:true,fresh_output:true,input_content_before_after_equal:true,shipping_arm64_elf_headers:true,
                      service_completed:false,binary_reproducibility:false,physical_device:false}}' > "$stage/BUILD-COMPLETION.json"
    (cd -- "$stage"; find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS)
    sync -f "$stage/SHA256SUMS"
    find "$stage" -type f -exec chmod 0444 {} +
    find "$stage" -type d -exec chmod 0555 {} +
    "$build_publisher" --new "$stage" "$job/receipt"
}
build_receipt_integrity() {
    local job=$1 class=$2
    [[ -d $job/receipt && ! -L $job/receipt && $(stat -c '%u %a' "$job/receipt") == "$UID 555" ]]
    jq -e --arg class "$class" --arg id "${job##*/}" \
        '.schema_version==1 and .evidence_class==$class and .job_id==$id and .state=="compiled" and
         .validation.command_completed and .validation.fresh_output and .validation.input_content_before_after_equal and
         .validation.shipping_arm64_elf_headers and (.validation.physical_device|not)' "$job/receipt/BUILD-COMPLETION.json" >/dev/null
    [[ -z $(find "$job/receipt" -type l -print -quit) && -z $(find "$job/receipt" -type f ! -perm 0444 -print -quit) ]]
    (cd -- "$job/receipt"; sha256sum -c SHA256SUMS >/dev/null)
    (cd -- "$job/receipt"; find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum) | cmp - "$job/receipt/SHA256SUMS"
}
build_publish() {
    local job=$1 destination=$2 class=$3
    build_receipt_integrity "$job" "$class"
    [[ $(build_digest "$build_publisher") == "$(jq -er .publisher_sha256 "$job/receipt/prepared.json")" ]]
    [[ -d $job/output && ! -L $job/output && ! -e $job/SERVICE-COMPLETION.json ]]
    [[ $(build_identity "$job/output") == "$(jq -er .output_identity "$job/receipt/BUILD-COMPLETION.json")" ]]
    [[ $(cat "$job/output/.uke-build-id") == "${job##*/}" ]]
    if [[ -e $destination || -L $destination ]]; then
        "$build_publisher" --exchange "$job/output" "$destination"
        mv -- "$job/output" "$job/previous-output"
    else
        "$build_publisher" --new "$job/output" "$destination"
    fi
}
build_verify() {
    local component=$1 android=$2 job=$3 output=$4 class=$5 work=$6 count
    build_receipt_integrity "$job" "$class"
    [[ -f $job/SERVICE-COMPLETION.json && ! -L $job/SERVICE-COMPLETION.json && $(stat -c '%u %a %h' "$job/SERVICE-COMPLETION.json") == "$UID 444 1" ]]
    jq -e --arg receipt "$(build_digest "$job/receipt/SHA256SUMS")" \
        '.schema_version==1 and .receipt_index_sha256==$receipt and .command_status==0 and .new_oom_events==0 and .published and
         .systemd_run_status==0 and .controller_confirmed' \
        "$job/SERVICE-COMPLETION.json" >/dev/null
    [[ -d $output && ! -L $output && $(build_identity "$output") == "$(jq -er .output_identity "$job/receipt/BUILD-COMPLETION.json")" ]]
    [[ $(cat "$output/.uke-build-id") == "${job##*/}" ]]
    count=$(jq -er .expected_project_count "$job/receipt/prepared.json")
    build_inputs "$component" "$android" "$work/inputs" "$count"
    build_same_inputs "$job/receipt/inputs" "$work/inputs"
    bash "$build_scripts/index-build-tree.sh" "$output" "$work/products" products
    diff -qr -- "$job/receipt/products" "$work/products" >/dev/null
    bash "$build_scripts/index-build-tree.sh" "$output/target/product/uke/recovery/root" "$work/payload" payload
    diff -qr -- "$job/receipt/payload" "$work/payload" >/dev/null
}
