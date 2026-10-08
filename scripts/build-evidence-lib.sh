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
    if [[ $count == 399 ]]; then
        # Production recovery has a reviewed localization inventory. Small
        # host-fixture jobs remain a distinct class and cannot export it.
        bash "$component/scripts/localization-evidence.sh" source > "$destination/localization.json"
    fi
    : > "$destination/host-tools.tsv"
    : > "$destination/host-runtime.tsv"
    # Upstream host Python is used by AOSP tooling only. Target payloads still
    # must pass the separate recursive no-Python audit. OS closure is not claimed.
    for command in bash awk bwrap ccache cp cpio c++ dd find git gzip jq ln make mv od perl python3 readelf rg sed sha256sum sort stat sync tar touch unzip xargs xmllint zip; do
        tool=$(command -v "$command")
        resolved=$(realpath -e -- "$tool")
        [[ -f $resolved && ! -L $resolved ]]
        printf '%s\t%s\n' "$command" "$(build_digest "$resolved")" >> "$destination/host-tools.tsv"
        # Installed trusted ELF host tools only; never ldd an input/target ELF.
        if [[ $(head -c 4 "$resolved" | od -An -tx1 | tr -d ' \n') == 7f454c46 ]]; then
            readelf --program-headers --wide "$resolved" > "$destination/host-elf.tmp"
            if ! rg -q '[[:space:]]INTERP[[:space:]]' "$destination/host-elf.tmp"; then
                # A trusted static tool has no loader or shared-library closure.
                # A non-interpreted ELF with unresolved NEEDED entries is invalid.
                readelf --dynamic --wide "$resolved" > "$destination/ldd.tmp"
                ! rg -q '\(NEEDED\)' "$destination/ldd.tmp"
                continue
            fi
            ldd "$resolved" > "$destination/ldd.tmp"
            ! rg -q 'not found' "$destination/ldd.tmp"
            while IFS= read -r runtime; do
                [[ -f $runtime ]]
                printf '%s\t%s\n' "${runtime##*/}" "$(build_digest "$(realpath -e -- "$runtime")")" >> "$destination/host-runtime.tsv"
            done < <(awk '{for(i=1;i<=NF;i++) if($i~/^\//) print $i}' "$destination/ldd.tmp")
        fi
    done
    rm -f -- "$destination/ldd.tmp" "$destination/host-elf.tmp"
    sort -u -o "$destination/host-runtime.tsv" "$destination/host-runtime.tsv"
    [[ ! -f /etc/os-release ]] || cp -- /etc/os-release "$destination/host-os-release"
    if command -v rpm >/dev/null; then rpm -qa --qf '%{NAME}\t%{EPOCHNUM}:%{VERSION}-%{RELEASE}.%{ARCH}\n' | sort > "$destination/host-package-versions.tsv"; fi
}
build_same_inputs() {
    diff -qr -- "$1" "$2" >/dev/null || { echo 'Build input content, modes, links, pins or host tools changed; no completion accepted.' >&2; return 1; }
}
build_input_index_integrity() {
    local inputs=$1 kind manifest
    [[ -d $inputs && ! -L $inputs && -s $inputs/revisions.tsv && -s $inputs/host-tools.tsv && -s $inputs/host-runtime.tsv ]]
    for kind in project android; do
        [[ -d $inputs/$kind && ! -L $inputs/$kind ]]
        for manifest in files.sha256 layout.bin external-links.tsv index.sha256; do
            [[ -f $inputs/$kind/$manifest && ! -L $inputs/$kind/$manifest ]]
        done
        sha256sum "$inputs/$kind/files.sha256" "$inputs/$kind/layout.bin" "$inputs/$kind/external-links.tsv" |
            awk '{print $1}' | cmp - "$inputs/$kind/index.sha256"
    done
}
build_cache_index() {
    local output=$1 destination=$2
    [[ -d $output && ! -L $output && ! -e $destination ]]
    mkdir -m 0700 -- "$destination"
    (
        cd -- "$output"
        # Bind even files omitted from final-product audits: cached compiler
        # objects, Ninja dependency state and generated build graphs.
        find . -printf '%p\t%y\t%m\t%s\t%T@\t%l\0' | sort -z -S 64M --parallel=1 > "$destination/layout.bin"
        awk -v RS='\0' -F '\t' 'NF!=6 || $1~/[\n\r]/ || $2!~/^[fdl]$/ || $3!~/^[0-7]{3,4}$/ ||
            $4!~/^[0-9]+$/ || $5!~/^[0-9]+\.[0-9]+$/ || $6~/[\n\r]/ {exit 1}' "$destination/layout.bin"
        find . -type f -print0 | sort -z -S 64M --parallel=1 | xargs -0 -r sha256sum > "$destination/files.sha256"
    )
    sha256sum "$destination/layout.bin" "$destination/files.sha256" | awk '{print $1}' > "$destination/index.sha256"
}
build_seed() {
    local component=$1 job=$2 producer=$3 class=$4 expected_root="${1}/build/android-builds" identity source path record provenance_before provenance_after
    [[ $producer == "$expected_root/${producer##*/}" && ${producer##*/} =~ ^job-[a-zA-Z0-9]{12}$ && $producer != "$job" ]]
    [[ -d $expected_root && ! -L $expected_root && $(stat -c '%u %a' "$expected_root") == "$UID 700" ]]
    [[ $(realpath -e -- "$producer") == "$producer" && ! -L $producer && $(stat -c '%u %a' "$producer") == "$UID 700" ]]
    [[ -f $producer/prepared.json && ! -L $producer/prepared.json && ! -e $producer/receipt && ! -e $producer/SERVICE-COMPLETION.json ]]
    [[ -d $producer/output && ! -L $producer/output && $(stat -c '%u %a' "$producer/output") == "$UID 700" ]]
    identity=$(build_identity "$producer/output")
    jq -e --arg class "$class" --arg identity "$identity" --slurpfile incoming "$job/prepared.json" \
        '.schema_version==1 and .evidence_class==$class and .state=="prepared" and .output_identity==$identity and
         .expected_project_count==$incoming[0].expected_project_count and .build.lunch==$incoming[0].build.lunch and
         .build.targets==$incoming[0].build.targets and .environment.output==$incoming[0].environment.output and
         .environment.source_date_epoch==$incoming[0].environment.source_date_epoch and
         .environment.build_datetime==$incoming[0].environment.build_datetime and
         .environment.build_number==$incoming[0].environment.build_number' "$producer/prepared.json" >/dev/null
    build_input_index_integrity "$producer/inputs-before"
    build_input_index_integrity "$job/inputs-before"
    provenance_before=$(sha256sum "$producer/prepared.json" "$producer/inputs-before/"{revisions.tsv,host-tools.tsv,host-runtime.tsv} \
        "$producer/inputs-before/"{project,android}/index.sha256 | sha256sum | cut -d' ' -f1)
    # Reviewed source changes are expected, but a cache cannot cross a source
    # lock, host compiler/runtime, lunch or firmware-profile boundary.
    for source in revisions.tsv host-tools.tsv host-runtime.tsv; do
        cmp -- "$producer/inputs-before/$source" "$job/inputs-before/$source"
    done
    if [[ $class == android-recovery ]]; then
        # Locked Git revisions alone do not cover staged/local prebuilt edits.
        # Reuse cannot cross a changed Android toolchain or embedded kernel.
        cmp <(awk '$2~/^\.\/prebuilts\// || $2~/^\.\/device\/xiaomi\/uke\/prebuilt\//' "$producer/inputs-before/android/files.sha256") \
            <(awk '$2~/^\.\/prebuilts\// || $2~/^\.\/device\/xiaomi\/uke\/prebuilt\//' "$job/inputs-before/android/files.sha256")
    fi
    for path in manifests/firmware.lock.json manifests/boot-profile-global.json manifests/boot-profile-cn.json; do
        source=$(awk -v path="$path" '$2==path {print $1}' "$producer/inputs-before/project/files.sha256")
        record=$(awk -v path="$path" '$2==path {print $1}' "$job/inputs-before/project/files.sha256")
        [[ $class != android-recovery || ( $source =~ ^[a-f0-9]{64}$ && $record =~ ^[a-f0-9]{64}$ ) ]]
        [[ $source == "$record" ]]
    done
    [[ ! -e $job/cache-seed ]]
    mkdir -m 0700 -- "$job/cache-seed"
    build_cache_index "$producer/output" "$job/cache-seed/producer-before"
    cp -a --reflink=always -- "$producer/output/." "$job/output/"
    [[ $(build_identity "$producer/output") == "$identity" ]]
    build_cache_index "$producer/output" "$job/cache-seed/producer-after"
    diff -qr -- "$job/cache-seed/producer-before" "$job/cache-seed/producer-after" >/dev/null
    provenance_after=$(sha256sum "$producer/prepared.json" "$producer/inputs-before/"{revisions.tsv,host-tools.tsv,host-runtime.tsv} \
        "$producer/inputs-before/"{project,android}/index.sha256 | sha256sum | cut -d' ' -f1)
    [[ $provenance_before == "$provenance_after" ]]
    build_cache_index "$job/output" "$job/cache-seed/copied"
    # Root directory metadata can differ because begin owns the new output.
    cmp -- "$job/cache-seed/producer-before/files.sha256" "$job/cache-seed/copied/files.sha256"
    awk -v RS='\0' -v ORS='\0' -F '\t' 'BEGIN {OFS="\t"} $2=="d" {$4="-"} {print}' \
        "$job/cache-seed/producer-before/layout.bin" > "$job/cache-seed/producer-layout.bin"
    awk -v RS='\0' -v ORS='\0' -F '\t' 'BEGIN {OFS="\t"} $2=="d" {$4="-"} {print}' \
        "$job/cache-seed/copied/layout.bin" > "$job/cache-seed/copied-layout.bin"
    cmp -- "$job/cache-seed/producer-layout.bin" "$job/cache-seed/copied-layout.bin"
    for path in target target/product target/product/uke; do
        [[ -d $job/output/$path && ! -L $job/output/$path && $(realpath -e -- "$job/output/$path") == "$job/output/$path" ]]
    done
    # Invalidate all installed payloads and final artifacts while retaining
    # obj/ and Soong intermediates. Ninja must reinstall and repack everything.
    : > "$job/cache-seed/invalidated-paths.txt"
    for path in recovery root system vendor oem odm product system_ext ramdisk debug_ramdisk symbols fake_packages kernel; do
        record="$job/output/target/product/uke/$path"
        if [[ -e $record || -L $record ]]; then
            printf 'target/product/uke/%s\n' "$path" >> "$job/cache-seed/invalidated-paths.txt"
            find "$record" -depth -delete
        fi
    done
    if [[ -d $job/output/target/product/uke ]]; then
        while IFS= read -r -d '' record; do
            printf '%s\n' "${record#"$job/output/"}" >> "$job/cache-seed/invalidated-paths.txt"
            unlink "$record"
        done < <(find "$job/output/target/product/uke" -maxdepth 1 \( -type f -o -type l \) \( -name '*.img' -o -name '*.zip' -o -name '*.tar' \) -print0)
    fi
    [[ ! -e $job/output/.uke-build-id && ! -L $job/output/.uke-build-id ]] || unlink "$job/output/.uke-build-id"
    # The output identity is new; producer identity and index are provenance,
    # never a previous successful compilation or service-completion claim.
    jq -cn --arg producer "${producer##*/}" --arg identity "$identity" \
        --arg prepared "$(build_digest "$producer/prepared.json")" \
        --arg inputs "$(build_digest "$producer/inputs-before/android/index.sha256")" \
        --arg project "$(build_digest "$producer/inputs-before/project/index.sha256")" \
        --arg index "$(build_digest "$job/cache-seed/producer-before/index.sha256")" \
        --arg invalidated "$(build_digest "$job/cache-seed/invalidated-paths.txt")" \
        '{schema_version:1,producer_job_id:$producer,producer_output_identity:$identity,producer_prepared_sha256:$prepared,
          producer_android_inputs_index_sha256:$inputs,producer_project_inputs_index_sha256:$project,producer_cache_index_sha256:$index,
          invalidated_paths_sha256:$invalidated,copy_method:"cp--reflink=always",producer_unchanged_during_copy:true,
          copied_file_contents_match:true,installed_payloads_invalidated:true,producer_success_claimed:false}' > "$job/cache-seed/SEED.json"
    jq --slurpfile seed "$job/cache-seed/SEED.json" --arg identity "$(build_identity "$job/output")" \
        '.output_identity=$identity | .output_origin={fresh_output:false,cache_seeded:true,seed:$seed[0]}' \
        "$job/prepared.json" > "$job/prepared.seeded.json"
    mv -- "$job/prepared.seeded.json" "$job/prepared.json"
}
build_begin() {
    local component=$1 android=$2 job=$3 class=$4 count=$5 jobs=$6 vm=$7 producer=${8:-}
    [[ ! -e $job && $class =~ ^(android-recovery|host-fixture)$ && $jobs =~ ^([1-9]|1[0-6])$ && $vm =~ ^[01]$ ]]
    mkdir -m 0700 -- "$job"
    mkdir -m 0700 -- "$job/output"
    build_inputs "$component" "$android" "$job/inputs-before" "$count"
    jq -cn --arg class "$class" --arg identity "$(build_identity "$job/output")" --arg publisher "$(build_digest "$build_publisher")" --argjson count "$count" \
        --arg go_procs "${GOMAXPROCS:-unspecified}" --arg go_heap "${GOMEMLIMIT:-unspecified}" --arg go_gc "${GOGC:-unspecified}" \
        --argjson jobs "$jobs" --argjson vm "$vm" \
        '{schema_version:1,evidence_class:$class,state:"prepared",output_identity:$identity,publisher_sha256:$publisher,expected_project_count:$count,
          output_origin:{fresh_output:true,cache_seeded:false},
          build:{lunch:"twrp_uke-bp2a-eng",targets:(if $vm==1 then ["recoveryimage","uke-btrfs-vm-fixture-soong"] else ["recoveryimage"] end),compile_jobs:$jobs},
          environment:{path:"/tmp/uke-build-launchers:/usr/bin:/bin",locale:"C",timezone:"UTC",home:"/tmp/uke-build-home",output:"/mnt/out-public",
            source_date_epoch:1790726400,build_datetime:1790726400,build_number:"uke-r12",host_python_write_bytecode:false,
            go_procs:$go_procs,go_heap:$go_heap,go_gc:$go_gc}}' \
        > "$job/prepared.json"
    [[ -z $producer ]] || build_seed "$component" "$job" "$producer" "$class"
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
    if jq -e '.output_origin.cache_seeded==true' "$job/prepared.json" >/dev/null; then
        cp -a -- "$job/cache-seed" "$stage/cache-seed"
    fi
    if [[ $(jq -er .evidence_class "$job/prepared.json") == android-recovery ]]; then
        [[ -s $job/admitted-policy.json && -s $job/host-temp-admitted.json ]]
        [[ -f $job/output/host/linux-x86/lib64/libc++.so && ! -L $job/output/host/linux-x86/lib64/libc++.so ]]
        cp -- "$job/admitted-policy.json" "$job/host-temp-admitted.json" "$stage/"
    fi
    printf '%s\n' "$id" > "$job/output/.uke-build-id"
    jq -cn --arg id "$id" --arg image "$(build_digest "$job/output/target/product/uke/recovery.img")" \
        --slurpfile prepared "$job/prepared.json" \
        --arg products "$(build_digest "$stage/products/index.sha256")" --arg payload "$(build_digest "$stage/payload/index.sha256")" \
        '{schema_version:1,job_id:$id,evidence_class:$prepared[0].evidence_class,state:"compiled",build:$prepared[0].build,environment:$prepared[0].environment,
          output_identity:$prepared[0].output_identity,recovery_image_sha256:$image,products_index_sha256:$products,payload_index_sha256:$payload,
          output_origin:($prepared[0].output_origin // {fresh_output:true,cache_seeded:false}),
          validation:{command_completed:true,fresh_output:(if $prepared[0].output_origin|has("fresh_output") then $prepared[0].output_origin.fresh_output else true end),
                      cache_seeded:($prepared[0].output_origin.cache_seeded // false),input_content_before_after_equal:true,shipping_arm64_elf_headers:true,
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
         .validation.command_completed and .validation.input_content_before_after_equal and
         ((.validation.fresh_output==true and (.validation.cache_seeded // false)==false) or
          (.validation.fresh_output==false and .validation.cache_seeded==true and
           .output_origin.cache_seeded==true and .output_origin.fresh_output==false and
           .output_origin.seed.installed_payloads_invalidated==true and .output_origin.seed.producer_success_claimed==false)) and
         .validation.shipping_arm64_elf_headers and (.validation.physical_device|not)' "$job/receipt/BUILD-COMPLETION.json" >/dev/null
    if jq -e '.validation.cache_seeded==true' "$job/receipt/BUILD-COMPLETION.json" >/dev/null; then
        [[ -s $job/receipt/cache-seed/SEED.json && ! -L $job/receipt/cache-seed/SEED.json ]]
        jq -e --slurpfile seed "$job/receipt/cache-seed/SEED.json" --slurpfile prepared "$job/receipt/prepared.json" \
            '.output_origin.seed==$seed[0] and .output_origin==$prepared[0].output_origin and
             $seed[0].copy_method=="cp--reflink=always" and $seed[0].producer_unchanged_during_copy==true and
             $seed[0].copied_file_contents_match==true' "$job/receipt/BUILD-COMPLETION.json" >/dev/null
        [[ $(build_digest "$job/receipt/cache-seed/producer-before/index.sha256") == \
            "$(jq -er .producer_cache_index_sha256 "$job/receipt/cache-seed/SEED.json")" ]]
        [[ $(build_digest "$job/receipt/cache-seed/invalidated-paths.txt") == \
            "$(jq -er .invalidated_paths_sha256 "$job/receipt/cache-seed/SEED.json")" ]]
    fi
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
