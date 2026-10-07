#!/usr/bin/env bash
# Host-only independent data/metadata oracles on private regular images.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source "$component/tests/f2fs-metadata-oracle-lib.sh"
binary=${UKE_RECOVERYCTL_BINARY:-$component/build/ure-host/uke-recoveryctl}
mkdir -p "$component/build/filesystem-data-fixtures"
fixture=$(mktemp -d "$component/build/filesystem-data-fixtures/run-XXXXXX")
passed=0
cleanup() {
    if [[ $passed == 1 ]]; then rm -rf -- "$fixture";
    else printf 'Private failed filesystem fixture retained: %s\n' "$fixture" >&2; fi
}
trap cleanup EXIT
export LC_ALL=C
call() {
    if ! "$binary" "$@" > "$fixture/result.json" || ! jq -e '.result=="ok"' "$fixture/result.json" >/dev/null; then
        cat "$fixture/result.json" >&2; return 1
    fi
}
rejected() {
    local expected=$1; shift
    if "$binary" "$@" > "$fixture/rejected.json"; then echo 'Expected a rejected filesystem operation' >&2; return 1; fi
    jq -e --arg codes "$expected" '.error.code as $actual | ($codes | split("|") | index($actual))!=null' "$fixture/rejected.json" >/dev/null || { cat "$fixture/rejected.json" >&2; return 1; }
}
digest() { sha256sum "$1" | cut -d' ' -f1; }
check_ext4() {
    local image=$1 log=$2 status=0
    e2fsck -f -p "$image" > "$log" 2>&1 || status=$?
    [[ $status == 0 || $status == 1 ]]
}
ext4_oracle() {
    local image=$1 output=$2
    mkdir -p "$output"
    debugfs -R "dump /payload.bin $output/payload.bin" "$image" > "$output/dump.log" 2>&1
    debugfs -R "dump /details.txt $output/details.txt" "$image" >> "$output/dump.log" 2>&1
    cmp "$fixture/payload.bin" "$output/payload.bin"
    cmp "$fixture/details.txt" "$output/details.txt"
    debugfs -R 'stat /details.txt' "$image" > "$output/details-stat.log" 2>&1
    debugfs -R 'stat /hardlink.txt' "$image" > "$output/hardlink-stat.log" 2>&1
    debugfs -R 'stat /symlink.txt' "$image" > "$output/symlink-stat.log" 2>&1
    local first second
    first=$(sed -n 's/^Inode: \([0-9]*\).*/\1/p' "$output/details-stat.log")
    second=$(sed -n 's/^Inode: \([0-9]*\).*/\1/p' "$output/hardlink-stat.log")
    [[ -n $first && $first == "$second" ]]
    rg -q 'Mode:  0640' "$output/details-stat.log"
    rg -q 'User: *12345 +Group: *54321' "$output/details-stat.log"
    rg -q '^Links: 2 ' "$output/details-stat.log"
    rg -q 'Fast link dest: "details.txt"' "$output/symlink-stat.log"
    debugfs -R "ea_get -f $output/xattr /details.txt user.ure" "$image" >> "$output/dump.log" 2>&1
    cmp "$fixture/xattr" "$output/xattr"
    dumpe2fs -h "$image" > "$output/super.log" 2>&1
    rg -q '^Filesystem volume name: *UREDATA$' "$output/super.log"
    rg -q '^Filesystem UUID: *11111111-2222-4333-8444-555555555555$' "$output/super.log"
    rg -q '^Filesystem features:.* quota' "$output/super.log"
    e2fsck -f -n "$image" > "$output/read-only-check.log" 2>&1
}
metadata() {
    local image=$1 commands=$2
    cat > "$commands" <<'COMMANDS'
set_inode_field /details.txt uid 12345
set_inode_field /details.txt gid 54321
set_inode_field /details.txt mode 0100640
set_inode_field /details.txt links_count 2
ea_set /details.txt user.ure fixture-metadata-value
COMMANDS
    debugfs -w -f "$commands" "$image" > "$commands.log" 2>&1
    check_ext4 "$image" "$commands.check.log"
}
printf 'Preserve recovery fixture metadata\n' > "$fixture/details.txt"
printf '%s' 'fixture-metadata-value' > "$fixture/xattr"
dd if=/dev/zero bs=1M count=80 status=none | tr '\0' R > "$fixture/payload.bin"
dd if=/dev/zero bs=1M count=4 status=none | tr '\0' F > "$fixture/filler.bin"
for arrangement in populated fragmented; do
    work="$fixture/$arrangement"; mkdir "$work"
    image="$work/source.img"; capacity=$((160*1024*1024)); sector=512
    if [[ $arrangement == fragmented ]]; then capacity=$((192*1024*1024)); sector=4096; fi
    truncate -s "$capacity" "$image"
    if [[ $arrangement == populated ]]; then
        mkdir "$work/tree"
        cp "$fixture/payload.bin" "$fixture/details.txt" "$work/tree/"
        ln "$work/tree/details.txt" "$work/tree/hardlink.txt"
        ln -s details.txt "$work/tree/symlink.txt"
        mke2fs -q -F -t ext4 -b 4096 -L UREDATA -U 11111111-2222-4333-8444-555555555555 \
            -O '^encrypt,^orphan_file,quota' -d "$work/tree" "$image" > "$work/format.log" 2>&1
    else
        mke2fs -q -F -t ext4 -b 4096 -L UREDATA -U 11111111-2222-4333-8444-555555555555 \
            -O '^encrypt,^orphan_file,quota' "$image" > "$work/format.log" 2>&1
        for i in {0..29}; do printf 'write "%s" /filler-%02d\n' "$fixture/filler.bin" "$i"; done > "$work/fill.commands"
        for ((i=0;i<30;i+=2)); do printf 'rm /filler-%02d\n' "$i"; done > "$work/holes.commands"
        {
            printf 'write "%s" /payload.bin\n' "$fixture/payload.bin"
            for ((i=1;i<30;i+=2)); do printf 'rm /filler-%02d\n' "$i"; done
            printf 'write "%s" /details.txt\nln /details.txt /hardlink.txt\nsymlink /symlink.txt details.txt\n' "$fixture/details.txt"
        } > "$work/payload.commands"
        for step in fill holes payload; do debugfs -w -f "$work/$step.commands" "$image" > "$work/$step.log" 2>&1; done
    fi
    metadata "$image" "$work/metadata.commands"
    ext4_oracle "$image" "$work/before"
    original=$(digest "$image"); inode=$(stat -c %i "$image")
    resize2fs -P "$image" > "$work/minimum-estimate.log" 2>&1
    minimum=$(sed -n 's/^Estimated minimum size of the filesystem: \([0-9]*\)$/\1/p' "$work/minimum-estimate.log")
    [[ $minimum =~ ^[0-9]+$ && $minimum -gt 8192 ]]
    cp --reflink=auto "$image" "$work/measured-minimum.img"
    resize2fs -M "$work/measured-minimum.img" > "$work/minimize.log" 2>&1
    measured=$(dumpe2fs -h "$work/measured-minimum.img" 2>/dev/null | sed -n 's/^Block count: *\([0-9]*\)$/\1/p')
    [[ $minimum == "$measured" ]]
    if [[ $arrangement == fragmented ]]; then
        debugfs -R 'stat /payload.bin' "$image" > "$work/fragmentation.log" 2>&1
        extents=$(rg -o '\([0-9]+(-[0-9]+)?\):[0-9]+(-[0-9]+)?' "$work/fragmentation.log" | wc -l)
        [[ $extents -gt 8 ]]
        debugfs -R 'blocks /payload.bin' "$image" > "$work/blocks.log" 2> "$work/blocks.stderr"
        awk -v boundary="$((minimum+1))" '{ for(i=1;i<=NF;i++) if($i~/^[0-9]+$/ && $i>=boundary) beyond++ } END {exit !(beyond>0)}' "$work/blocks.log"
    fi
    for delta in -1 0 1; do
        case_name="boundary-$delta"; bytes=$(((minimum+delta)*4096))
        jq -n --argjson bytes "$bytes" '{schema:1,action:"resize",filesystem:"ext4",target_bytes:$bytes}' > "$work/$case_name.request.json"
        call filesystem plan "$work/$case_name.request.json" --image "$image" --sector-size "$sector" --profile fixture-profile --output "$work/$case_name.plan.json"
        hash=$(jq -r .plan_sha256 "$work/$case_name.plan.json")
        if [[ $delta == -1 ]]; then
            rejected filesystem-tool-failed filesystem execute "$work/$case_name.plan.json" --image "$image" --sector-size "$sector" --journal "$work/$case_name.job" --confirm "$hash"
            [[ $(digest "$image") == "$original" ]]
            call filesystem inspect-journal "$work/$case_name.job" --image "$image" --sector-size "$sector"
            jq -e '.data.state=="FAILED_SAFE" and .data.original_unchanged_verified==true' "$fixture/result.json" >/dev/null
            call filesystem cancel "$work/$case_name.job" --image "$image" --sector-size "$sector" --confirm "$hash"
        else
            call filesystem execute "$work/$case_name.plan.json" --image "$image" --sector-size "$sector" --journal "$work/$case_name.job" --confirm "$hash"
            jq -e '.data.state=="COMPLETE" and .data.physical_test_record==false' "$fixture/result.json" >/dev/null
            [[ $(stat -c %s "$image") == "$capacity" && $(stat -c %i "$image") == "$inode" ]]
            ext4_oracle "$image" "$work/$case_name.after"
            actual=$(sed -n 's/^Block count: *\([0-9]*\)$/\1/p' "$work/$case_name.after/super.log")
            [[ $actual == "$((minimum+delta))" ]]
            call filesystem rollback "$work/$case_name.job" --image "$image" --sector-size "$sector" --confirm "$hash"
        fi
        [[ $(digest "$image") == "$original" && $(stat -c %i "$image") == "$inode" ]]
    done
    jq -n '{schema:1,action:"repair",filesystem:"ntfs"}' > "$work/wrong-type.request.json"
    rejected filesystem-mismatch filesystem plan "$work/wrong-type.request.json" --image "$image" --sector-size "$sector" --profile fixture-profile --output "$work/wrong-type.plan.json"
    [[ ! -e $work/wrong-type.plan.json && $(digest "$image") == "$original" ]]
    jq -n --argjson bytes "$((capacity+4096))" '{schema:1,action:"resize",filesystem:"ext4",target_bytes:$bytes}' > "$work/beyond-container.request.json"
    rejected invalid-resize-size filesystem plan "$work/beyond-container.request.json" --image "$image" --sector-size "$sector" --profile fixture-profile --output "$work/beyond-container.plan.json"
    [[ ! -e $work/beyond-container.plan.json && $(digest "$image") == "$original" ]]
    cp --reflink=auto "$image" "$work/clean.img"
    debugfs -w -R 'set_inode_field /details.txt links_count 7' "$image" > "$work/damage.log" 2>&1
    damaged=$(digest "$image"); [[ $damaged != "$original" ]]
    damage_status=0
    e2fsck -f -n "$image" > "$work/damaged-check.log" 2>&1 || damage_status=$?
    [[ $damage_status == 4 ]]
    jq -n '{schema:1,action:"repair",filesystem:"ext4"}' > "$work/repair.request.json"
    call filesystem plan "$work/repair.request.json" --image "$image" --sector-size "$sector" --profile fixture-profile --output "$work/repair.plan.json"
    hash=$(jq -r .plan_sha256 "$work/repair.plan.json")
    call filesystem execute "$work/repair.plan.json" --image "$image" --sector-size "$sector" --journal "$work/repair.job" --confirm "$hash"
    jq -e '.data.tool_result.exit_status==1' "$fixture/result.json" >/dev/null
    ext4_oracle "$image" "$work/repaired"
    call filesystem rollback "$work/repair.job" --image "$image" --sector-size "$sector" --confirm "$hash"
    [[ $(digest "$image") == "$damaged" && $(stat -c %i "$image") == "$inode" ]]
    # Independent setup is undone only after its damaged original was verified.
    dd if="$work/clean.img" of="$image" bs=1M conv=notrunc status=none
    [[ $(digest "$image") == "$original" ]]
    printf '%s ext4: minimum=%s blocks, sector=%s, payload/metadata, real damage, retained size and exact rollback passed.\n' "$arrangement" "$minimum" "$sector"
done
other_oracle() {
    local type=$1 image=$2 output=$3
    mkdir -p "$output"
    case "$type" in
        ntfs)
            # ntfsresize deliberately schedules Windows chkdsk. Force only
            # these read-only host oracles; do not clear its dirty/check flag.
            ntfscat -f "$image" /payload.bin > "$output/payload.bin"
            ntfscat -f "$image" /details.txt > "$output/details.txt"
            ntfsinfo -f -m "$image" > "$output/volume.log" 2>&1
            rg -q 'Volume Name: UREDATA$' "$output/volume.log"
            ntfsfix -n "$image" > "$output/check.log" 2>&1
            od -An -tx1 -j 72 -N 8 "$image" > "$output/identity"
            ;;
        vfat)
            mcopy -i "$image" ::PAYLOAD.BIN "$output/payload.bin"
            mcopy -i "$image" ::DETAILS.TXT "$output/details.txt"
            mdir -i "$image" :: > "$output/volume.log" 2>&1
            rg -q 'Volume in drive : is UREDATA' "$output/volume.log"
            fsck.fat -n "$image" > "$output/check.log" 2>&1
            od -An -tx1 -j 67 -N 15 "$image" > "$output/identity"
            ;;
        f2fs)
            # The host's older dump tool cannot recursively dump the root. Use
            # its own NAT inventory to select regular inode records instead.
            (
                cd "$output"
                dump.f2fs -n 0~32 "$image" > inventory.log 2>&1
                mapfile -t inodes < <(awk '$1=="nid:" && $3=="ino:" && $2==$4 && $2>3 {print $2}' dump_nat)
                [[ ${#inodes[@]} == 2 ]]
                for inode in "${inodes[@]}"; do
                    printf 'y\n' | dump.f2fs -i "$(printf '%x' "$inode")" "$image" > "inode-$inode.log" 2>&1
                done
                mv lost_found/payload.bin payload.bin
                mv lost_found/details.txt details.txt
                # Dump traversal and rg's multi-file scheduling need not retain
                # inode order. Bind all five fields to the on-disk filename.
                ure_f2fs_fixture_metadata inode-*.log > metadata
            )
            fsck.f2fs --dry-run "$image" > "$output/check.log" 2>&1
            # Pinned on-disk superblock: UUID at 108, UTF-16 label at 124.
            od -An -tx1 -j 1132 -N 1040 "$image" > "$output/identity"
            ;;
    esac
    cmp "$fixture/payload.bin" "$output/payload.bin"
    cmp "$fixture/details.txt" "$output/details.txt"
}
for type in ntfs f2fs vfat; do
    work="$fixture/$type"; mkdir "$work"
    image="$work/source.img"; capacity=$((192*1024*1024)); resized=$((128*1024*1024)); sector=512
    if [[ $type != ntfs ]]; then capacity=$((512*1024*1024)); resized=$((256*1024*1024)); sector=4096; fi
    truncate -s "$capacity" "$image"
    case "$type" in
        ntfs)
            mkfs.ntfs -F -Q -L UREDATA "$image" > "$work/format.log" 2>&1
            ntfscp "$image" "$fixture/payload.bin" /payload.bin
            ntfscp "$image" "$fixture/details.txt" /details.txt
            ;;
        f2fs)
            mkdir "$work/tree"; cp "$fixture/payload.bin" "$fixture/details.txt" "$work/tree/"
            mkfs.f2fs -f -l UREDATA "$image" > "$work/format.log" 2>&1
            sload.f2fs -f "$work/tree" -P "$image" > "$work/load.log" 2>&1
            ;;
        vfat)
            resized=$((384*1024*1024))
            mkfs.fat -F 32 -n UREDATA "$image" > "$work/format.log" 2>&1
            dd if=/dev/zero bs=1M count=360 status=none | tr '\0' F > "$work/filler.bin"
            mcopy -i "$image" "$work/filler.bin" ::FILLER.BIN
            mcopy -i "$image" "$fixture/payload.bin" ::PAYLOAD.BIN
            mcopy -i "$image" "$fixture/details.txt" ::DETAILS.TXT
            mdel -i "$image" ::FILLER.BIN
            mshowfat -i "$image" ::PAYLOAD.BIN > "$work/allocations.log" 2>&1
            # Default mkfs geometry for this fixture uses eight sectors/cluster;
            # independently require a file allocation above the resize boundary.
            sectors_per_cluster=$(od -An -tu1 -j 13 -N 1 "$image" | tr -d ' ')
            awk -v boundary="$((resized/512/sectors_per_cluster))" '{gsub(/[^0-9]+/," ");for(i=1;i<=NF;i++)if($i>boundary)beyond++}END{exit !(beyond>0)}' "$work/allocations.log"
            ;;
    esac
    other_oracle "$type" "$image" "$work/before"
    original=$(digest "$image"); inode=$(stat -c %i "$image")
    # Occupied data alone exceeds this request. The tool must fail on staging.
    if [[ $type != vfat ]]; then
        jq -n --arg type "$type" '{schema:1,action:"resize",filesystem:$type,target_bytes:67108864}' > "$work/too-small.request.json"
        call filesystem plan "$work/too-small.request.json" --image "$image" --sector-size "$sector" --profile fixture-profile --output "$work/too-small.plan.json"
        hash=$(jq -r .plan_sha256 "$work/too-small.plan.json")
        expected_failure=filesystem-tool-failed
        if [[ $type == f2fs ]]; then expected_failure+='|filesystem-resize-geometry-mismatch'; fi
        rejected "$expected_failure" filesystem execute "$work/too-small.plan.json" --image "$image" --sector-size "$sector" --journal "$work/too-small.job" --confirm "$hash"
        call filesystem cancel "$work/too-small.job" --image "$image" --sector-size "$sector" --confirm "$hash"
        [[ $(digest "$image") == "$original" ]]
    fi
    jq -n --arg type "$type" --argjson bytes "$resized" '{schema:1,action:"resize",filesystem:$type,target_bytes:$bytes}' > "$work/resize.request.json"
    call filesystem plan "$work/resize.request.json" --image "$image" --sector-size "$sector" --profile fixture-profile --output "$work/resize.plan.json"
    hash=$(jq -r .plan_sha256 "$work/resize.plan.json")
    call filesystem execute "$work/resize.plan.json" --image "$image" --sector-size "$sector" --journal "$work/resize.job" --confirm "$hash"
    jq -e '.data.state=="COMPLETE" and .data.physical_test_record==false' "$fixture/result.json" >/dev/null
    other_oracle "$type" "$image" "$work/after"
    if [[ $type == f2fs ]]; then
        actual_blocks=$(od -An -tu8 -j 1060 -N 8 "$image" | tr -d ' ')
        [[ $((actual_blocks*4096)) == "$resized" ]]
        jq -e --argjson bytes "$resized" '.data.resized_filesystem_geometry.requested_size_verified==true and .data.resized_filesystem_geometry.filesystem_bytes==$bytes' "$fixture/result.json" >/dev/null
    fi
    cmp "$work/before/identity" "$work/after/identity"
    if [[ $type == f2fs ]]; then cmp "$work/before/metadata" "$work/after/metadata"; fi
    [[ $(stat -c %s "$image") == "$capacity" && $(stat -c %i "$image") == "$inode" ]]
    call filesystem rollback "$work/resize.job" --image "$image" --sector-size "$sector" --confirm "$hash"
    [[ $(digest "$image") == "$original" && $(stat -c %i "$image") == "$inode" ]]
    printf '%s: populated shrink, independent payload/identity/label, retained capacity and complete rollback passed.\n' "$type"
done
passed=1
