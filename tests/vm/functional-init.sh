#!/system/bin/sh
# Disposable generic-virt guest only. The production CLI is not relinked.
set -eu
export PATH=/system/bin
export LD_LIBRARY_PATH=/system/lib64:/vendor/lib64:/system/lib64/bootstrap
export MKE2FS_CONFIG=/system/etc/mke2fs.conf
export TMPDIR=/tmp
umask 077
fail() {
    trap - EXIT
    echo "URE_FUNCTION_FAILURE $1"
    sync
    if [ -w /proc/sysrq-trigger ]; then echo o > /proc/sysrq-trigger; fi
    toybox reboot -p -f
    exit 1
}
trap 'fail unexpected-shell-status-$?' EXIT
/system/bin/toybox mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mount -t tmpfs tmpfs /run
mount -t tmpfs tmpfs /tmp
grep -q linux,dummy-virt /sys/firmware/devicetree/base/compatible
grep -q ure_function_fixture=1 /proc/cmdline
fixture_disk() {
    wanted=$1
    selected_disk=''
    for serial_file in /sys/class/block/vd*/serial; do
        [ -r "$serial_file" ] || continue
        [ "$(cat "$serial_file")" = "$wanted" ] || continue
        [ -z "$selected_disk" ] || fail duplicate-fixture-disk
        disk_path=${serial_file%/serial}
        selected_disk="/dev/${disk_path##*/}"
    done
    [ -n "$selected_disk" ] && [ -b "$selected_disk" ] || fail missing-fixture-disk
    printf '%s\n' "$selected_disk"
}
media_disk=$(fixture_disk ure-functional-media)
mount -t ext4 "$media_disk" /media
mkdir -p /media/records
ctl=/system/bin/uke-recoveryctl
mode=$(cat /ure-function-group)
emit() {
    echo "URE_FUNCTION_JSON_BEGIN $1"
    cat "/media/records/$1.json"
    echo "URE_FUNCTION_JSON_END $1"
}
call() {
    record=$1; shift
    status=0
    "$ctl" "$@" > "/media/records/$record.json" 2> "/media/records/$record.err" || status=$?
    emit "$record"
    cat "/media/records/$record.err"
    [ "$status" = 0 ] || fail "$record"
}
reject() {
    record=$1; shift
    if "$ctl" "$@" > "/media/records/$record.json" 2> "/media/records/$record.err"; then fail "$record-accepted"; fi
    emit "$record"
    cat "/media/records/$record.err"
}
plan_digest() { awk -F '"' '/^  "plan_sha256"/ { print $4 }' "$1"; }
digest() { sha256sum "$1" | awk '{print $1}'; }
check() { echo "URE_FUNCTION_CHECK $1"; }

case "$mode" in
core)
    call capabilities capabilities
    call filesystem-capabilities filesystem capabilities
    reject block-path-refused filesystem inspect --image /dev/vda
    reject unrelated-options help --confirm ignored
    home_relative=home/user
    fixture_home="/media/linux/$home_relative"
    mkdir -p /media/linux/etc "$fixture_home/documents" /media/output
    printf 'ID=arch\nNAME="Synthetic Arch root"\n' > /media/linux/etc/os-release
    printf 'UUID=fixture-root / ext4 defaults 0 1\n' > /media/linux/etc/fstab
    cp /media/linux/etc/fstab /media/fstab-before
    printf 'UUID=fixture-root / ext4 ro 0 1\n' > /media/fstab-after
    call arch-detect linux detect --root /media/linux
    call files-list files list etc --root /media/linux
    call files-search files search . fstab --root /media/linux
    reject traversal-refused files list ../ --root /media/linux
    call editor-plan editor plan etc/fstab --root /media/linux --content-file /media/fstab-after --profile vm-fixture --output /media/editor-plan.json
    confirmation=$(plan_digest /media/editor-plan.json)
    [ ${#confirmation} = 64 ] || fail editor-confirmation
    reject editor-wrong-confirm transaction execute /media/editor-plan.json --root /media/linux --journal /media/rejected-editor --confirm wrong
    [ ! -e /media/rejected-editor ] || fail editor-rejected-store
    cmp /media/fstab-before /media/linux/etc/fstab || fail editor-refusal-bytes
    call editor-execute transaction execute /media/editor-plan.json --root /media/linux --journal /media/editor-job --confirm "$confirmation"
    cmp /media/fstab-after /media/linux/etc/fstab || fail editor-after-bytes
    call editor-inspect transaction inspect /media/editor-job --root /media/linux
    call editor-rollback transaction rollback /media/editor-job --root /media/linux --confirm "$confirmation"
    cmp /media/fstab-before /media/linux/etc/fstab || fail editor-original-bytes
    check editor-commit-inspect-exact-rollback
    printf 'ID=fedora\nNAME="Synthetic Fedora root"\n' > /media/linux/etc/os-release
    call fedora-detect linux detect --root /media/linux
    for percent in 50 55 60 65 70 75 80 85 90 95 100; do
        call "scale-save-$percent" display settings-save /media/scale "$percent"
        call "scale-load-$percent" display settings-load /media/scale
    done
    before=$(digest /media/scale/display.json)
    reject scale-invalid display settings-save /media/scale 49
    [ "$before" = "$(digest /media/scale/display.json)" ] || fail scale-invalid-bytes
    [ "$(stat -c %a /media/scale)" = 700 ] && [ "$(stat -c %a /media/scale/display.json)" = 600 ] || fail scale-permissions
    check eleven-private-scale-roundtrips
    for sector in 512 4096; do
        image="/media/raw-$sector.img"
        truncate -s 4194304 "$image"
        printf 'synthetic original raw content\n' | dd of="$image" conv=notrunc 2>/dev/null
        desired=$(digest "$image")
        call "raw-$sector-plan" backup storage-plan --image "$image" --sector-size "$sector" --profile vm-fixture --chunk-size 65536 --output "/media/raw-$sector-plan.json"
        call "raw-$sector-capture" backup capture "/media/raw-$sector-plan.json" --journal "/media/raw-$sector-backup"
        rm "/media/raw-$sector-backup/chunk-00001.bin"
        reject "raw-$sector-hole" backup verify "/media/raw-$sector-backup"
        for chunk in "/media/raw-$sector-backup"/chunk-*.bin; do
            [ "${chunk##*/}" = chunk-00000.bin ] || rm "$chunk"
        done
        call "raw-$sector-partial" backup verify "/media/raw-$sector-backup"
        call "raw-$sector-resume" backup resume "/media/raw-$sector-backup"
        call "raw-$sector-verify" backup verify "/media/raw-$sector-backup"
        "$ctl" backup store-export "/media/raw-$sector-backup" --chunk 0 > /media/export.bin || fail raw-export
        cmp /media/export.bin "/media/raw-$sector-backup/chunk-00000.bin" || fail raw-export-framing
        printf 'replacement bytes to retain for rollback\n' | dd of="$image" conv=notrunc 2>/dev/null
        before=$(digest "$image")
        call "raw-$sector-restore-plan" restore plan "/media/raw-$sector-backup" --image "$image" --sector-size "$sector" --profile vm-fixture --output "/media/raw-$sector-restore.json"
        confirmation=$(plan_digest "/media/raw-$sector-restore.json")
        reject "raw-$sector-wrong-confirm" restore execute "/media/raw-$sector-restore.json" --image "$image" --sector-size "$sector" --journal "/media/raw-$sector-rejected" --confirm wrong
        [ ! -e "/media/raw-$sector-rejected" ] && [ "$before" = "$(digest "$image")" ] || fail raw-refusal-bytes
        call "raw-$sector-restore" restore execute "/media/raw-$sector-restore.json" --image "$image" --sector-size "$sector" --journal "/media/raw-$sector-job" --confirm "$confirmation"
        [ "$desired" = "$(digest "$image")" ] || fail raw-restored-bytes
        call "raw-$sector-inspect" restore inspect "/media/raw-$sector-job" --image "$image" --sector-size "$sector"
        mv "/media/raw-$sector-backup" "/media/raw-$sector-offline"
        call "raw-$sector-rollback" restore rollback "/media/raw-$sector-job" --image "$image" --sector-size "$sector" --confirm "$confirmation"
        [ "$before" = "$(digest "$image")" ] || fail raw-rollback-bytes
        check "raw-$sector-resume-export-restore-source-independent-rollback"
    done
    printf 'home content\n' > "$fixture_home/documents/data"
    ln "$fixture_home/documents/data" "$fixture_home/hardlink"
    ln -s /unavailable/target "$fixture_home/absolute-link"
    mkfifo -m 640 "$fixture_home/pipe"
    chmod 640 "$fixture_home/pipe"
    truncate -s 16777216 "$fixture_home/sparse"
    printf 'sparse extent\n' | dd of="$fixture_home/sparse" bs=1 seek=8388608 conv=notrunc 2>/dev/null
    opaque=$(printf 'line\nbreak')
    printf 'opaque name\n' > "$fixture_home/$opaque"
    chmod 750 "$fixture_home/documents"
    setfattr -n user.ure-test -v preserved-xattr "$fixture_home/documents/data"
    call home-plan backup tree-plan home/user --root /media/linux --profile vm-fixture --output /media/home-store
    confirmation=$(plan_digest /media/home-store/plan.json)
    reject home-wrong-confirm backup tree-capture /media/home-store --root /media/linux --confirm wrong
    reject home-before-capture backup tree-restore /media/home-store --output /media/output/premature --confirm "$confirmation"
    [ ! -e /media/output/premature ] || fail premature-home-target
    call home-capture backup tree-capture /media/home-store --root /media/linux --confirm "$confirmation"
    call home-verify backup tree-verify /media/home-store
    call home-restore backup tree-restore /media/home-store --output /media/output/restored --confirm "$confirmation"
    restored=/media/output/restored
    cmp "$fixture_home/documents/data" "$restored/documents/data" && cmp "$fixture_home/sparse" "$restored/sparse" && cmp "$fixture_home/$opaque" "$restored/$opaque" || fail home-content
    [ "$(stat -c %i "$restored/documents/data")" = "$(stat -c %i "$restored/hardlink")" ] || fail home-hardlink
    [ "$(readlink "$restored/absolute-link")" = /unavailable/target ] && [ -p "$restored/pipe" ] || fail home-special-files
    [ "$(stat -c %a "$restored/pipe")" = 640 ] && [ "$(stat -c %a "$restored/documents")" = 750 ] || fail home-mode
    [ "$(stat -c %b "$restored/sparse")" -lt 1000 ] || fail home-sparse-allocation
    [ "$(getfattr --only-values -n user.ure-test "$restored/documents/data")" = preserved-xattr ] || fail home-xattr
    reject home-existing-target backup tree-restore /media/home-store --output "$restored" --confirm "$confirmation"
    check home-content-hardlinks-symlinks-fifo-mode-xattr-sparse-and-opaque-names
    printf 'source has changed\n' >> "$fixture_home/documents/data"
    reject home-stale-source backup tree-capture /media/home-store --root /media/linux --confirm "$confirmation"
    call home-independent-verify backup tree-verify /media/home-store
    cp /media/home-store/entries-0.json /media/home-page-before
    printf 'corrupt\n' > /media/home-store/entries-0.json
    reject home-corrupt-pages backup tree-restore /media/home-store --output /media/output/corrupt --confirm "$confirmation"
    [ ! -e /media/output/corrupt ] || fail corrupt-home-target
    cp /media/home-page-before /media/home-store/entries-0.json
    mkdir /media/resume-source
    for index in $(seq 1 650); do printf 'resume %s\n' "$index" > "/media/resume-source/file-$index"; done
    call home-resume-plan backup tree-plan . --root /media/resume-source --profile vm-fixture --output /media/resume-store
    confirmation=$(plan_digest /media/resume-store/plan.json)
    "$ctl" backup tree-capture /media/resume-store --root /media/resume-source --confirm "$confirmation" > /media/interrupted.json 2> /media/interrupted.err &
    writer=$!
    observed=false
    for attempt in $(seq 1 1000); do
        if [ -n "$(find /media/resume-store -maxdepth 1 -name 'data-*.bin' -print -quit)" ] && kill -0 "$writer" 2>/dev/null; then observed=true; break; fi
        kill -0 "$writer" 2>/dev/null || break
        sleep 0.02
    done
    "$observed" || fail home-capture-not-observed
    kill -KILL "$writer"
    if wait "$writer"; then fail home-sigkill-not-observed; fi
    call home-interrupted backup tree-inspect /media/resume-store
    call home-resumed backup tree-capture /media/resume-store --root /media/resume-source --confirm "$confirmation"
    call home-resumed-verify backup tree-verify /media/resume-store
    check observed-home-sigkill-verified-resume
    ;;
filesystems)
    call filesystem-capabilities filesystem capabilities
    for type in ext4 exfat ntfs vfat f2fs; do
        directory="/media/$type"
        mkdir "$directory"
        image="$directory/source.img"
        capacity=67108864
        [ "$type" != vfat ] || capacity=134217728
        [ "$type" != f2fs ] || capacity=536870912
        truncate -s "$capacity" "$image"
        original=$(digest "$image"); inode=$(stat -c %i "$image")
        printf '{"schema":1,"action":"format","filesystem":"%s","erase_confirmed":true,"label":"URETEST"}\n' "$type" > "$directory/request.json"
        call "fs-$type-plan" filesystem plan "$directory/request.json" --image "$image" --profile vm-fixture --output "$directory/plan.json"
        confirmation=$(plan_digest "$directory/plan.json")
        reject "fs-$type-wrong-confirm" filesystem execute "$directory/plan.json" --image "$image" --journal "$directory/rejected" --confirm wrong
        [ ! -e "$directory/rejected" ] && [ "$original" = "$(digest "$image")" ] || fail filesystem-refusal-bytes
        call "fs-$type-format" filesystem execute "$directory/plan.json" --image "$image" --journal "$directory/job" --confirm "$confirmation"
        call "fs-$type-inspect" filesystem inspect --image "$image"
        call "fs-$type-journal" filesystem inspect-journal "$directory/job" --image "$image"
        formatted=$(digest "$image")
        printf '{"schema":1,"action":"repair","filesystem":"%s"}\n' "$type" > "$directory/repair.json"
        call "fs-$type-repair-plan" filesystem plan "$directory/repair.json" --image "$image" --profile vm-fixture --output "$directory/repair-plan.json"
        repair_confirmation=$(plan_digest "$directory/repair-plan.json")
        if [ "$type" = ext4 ]; then
            # devtmpfs on generic virt exposes /dev/loop0; Android Toybox's
            # automatic lookup returns a /dev/block alias absent in this guest.
            loop=/dev/loop0
            [ -b "$loop" ] || fail missing-fixture-loop
            losetup "$loop" "$image"
            reject fs-ext4-live-loop filesystem execute "$directory/repair-plan.json" --image "$image" --journal "$directory/busy-job" --confirm "$repair_confirmation"
            [ ! -e "$directory/busy-job" ] && [ "$formatted" = "$(digest "$image")" ] || fail filesystem-loop-refusal-bytes
            losetup -d "$loop"
            check filesystem-active-loop-alias-refused-before-write
        fi
        call "fs-$type-repair" filesystem execute "$directory/repair-plan.json" --image "$image" --journal "$directory/repair-job" --confirm "$repair_confirmation"
        call "fs-$type-repair-rollback" filesystem rollback "$directory/repair-job" --image "$image" --confirm "$repair_confirmation"
        [ "$formatted" = "$(digest "$image")" ] || fail filesystem-repair-rollback-bytes
        check "filesystem-$type-repair-exact-rollback"
        case "$type" in
        ext4|ntfs|f2fs)
            formatted=$(digest "$image")
            printf '{"schema":1,"action":"resize","filesystem":"%s","target_bytes":%s}\n' "$type" "$((capacity/2))" > "$directory/resize.json"
            call "fs-$type-resize-plan" filesystem plan "$directory/resize.json" --image "$image" --profile vm-fixture --output "$directory/resize-plan.json"
            resize_confirmation=$(plan_digest "$directory/resize-plan.json")
            call "fs-$type-resize" filesystem execute "$directory/resize-plan.json" --image "$image" --journal "$directory/resize-job" --confirm "$resize_confirmation"
            [ "$(stat -c %i "$image")" = "$inode" ] && [ "$(stat -c %s "$image")" = "$capacity" ] || fail filesystem-image-identity
            call "fs-$type-resize-rollback" filesystem rollback "$directory/resize-job" --image "$image" --confirm "$resize_confirmation"
            [ "$formatted" = "$(digest "$image")" ] || fail filesystem-resize-rollback-bytes
            check "filesystem-$type-resize-exact-rollback"
            ;;
        exfat|vfat)
            formatted=$(digest "$image")
            printf '{"schema":1,"action":"resize","filesystem":"%s","target_bytes":%s}\n' "$type" "$((capacity*3/4))" > "$directory/resize.json"
            reject "fs-$type-resize-unavailable" filesystem plan "$directory/resize.json" --image "$image" --profile vm-fixture --output "$directory/unavailable-plan.json"
            [ ! -e "$directory/unavailable-plan.json" ] && [ "$formatted" = "$(digest "$image")" ] || fail filesystem-unavailable-bytes
            check "filesystem-$type-unavailable-resize-preserves-bytes"
            ;;
        esac
        call "fs-$type-rollback" filesystem rollback "$directory/job" --image "$image" --confirm "$confirmation"
        [ "$original" = "$(digest "$image")" ] && [ "$(stat -c %i "$image")" = "$inode" ] && [ "$(stat -c %s "$image")" = "$capacity" ] || fail filesystem-format-rollback-bytes
        check "filesystem-$type-format-inspect-exact-rollback"
        rm -rf "$directory"
    done
    ;;
rescue)
    selected=/media/rescue-root
    selected_esp=/media/rescue-esp
    mkdir -p "$selected"/usr/bin "$selected"/etc "$selected"/boot/efi "$selected"/system/bin "$selected"/proc "$selected"/sys "$selected"/dev "$selected"/run "$selected"/tmp "$selected"/root "$selected"/var "$selected_esp"
    cp -a /system/lib64 "$selected/system/"
    cp /system/bin/linker64 "$selected/system/bin/"
    cp /system/bin/bash "$selected/usr/bin/bash"
    cp /system/bin/toybox "$selected/usr/bin/sleep"
    printf 'ID=arch\nNAME="Synthetic Arch shell root"\n' > "$selected/etc/os-release"
    printf 'UUID=fixture-root / ext4 defaults 0 1\nUUID=fixture-esp /boot/efi vfat defaults 0 2\n' > "$selected/etc/fstab"
    printf 'selected ESP\n' > "$selected_esp/marker"
    cat > /media/rescue-readonly.json <<'REQUEST'
{"schema":1,"action":"shell","write":false,"network":false,"timeout_seconds":15,"shell_input":"test -d /proc/1 && test -d /sys/devices && test -c /dev/null && test -f /boot/efi/marker && test ! -b /dev/vda || exit 7\nif printf bad > /etc/forbidden; then exit 8; fi\nif printf bad > /boot/efi/marker; then exit 9; fi\nprintf runtime > /run/session-marker\nprintf URE_MANAGED_READONLY\n/usr/bin/sleep 30 &\nexit 0\n"}
REQUEST
    before_mounts=$(cat /proc/self/mountinfo)
    call rescue-readonly-plan linux rescue-plan /media/rescue-readonly.json --root "$selected" --esp "$selected_esp" --output /media/rescue-readonly-plan.json
    confirmation=$(plan_digest /media/rescue-readonly-plan.json)
    call rescue-readonly linux rescue-execute /media/rescue-readonly-plan.json --root "$selected" --esp "$selected_esp" --journal /media/rescue-readonly-job --confirm "$confirmation"
    grep -q URE_MANAGED_READONLY /media/rescue-readonly-job/console.log || fail rescue-shell-console
    [ ! -e "$selected/etc/forbidden" ] && [ ! -e "$selected/run/session-marker" ] && [ "$(cat "$selected_esp/marker")" = 'selected ESP' ] || fail rescue-readonly-preservation
    [ "$before_mounts" = "$(cat /proc/self/mountinfo)" ] && [ ! -e /media/rescue-readonly-job/mount-root ] || fail rescue-readonly-cleanup
    check managed-chroot-readonly-selected-esp-and-descendant-cleanup
    printf '%s\n' '{"schema":1,"action":"shell","write":true,"network":false,"timeout_seconds":15,"shell_input":"printf repaired > /etc/repaired\n"}' > /media/rescue-write.json
    call rescue-write-plan linux rescue-plan /media/rescue-write.json --root "$selected" --esp "$selected_esp" --output /media/rescue-write-plan.json
    confirmation=$(plan_digest /media/rescue-write-plan.json)
    call rescue-write linux rescue-execute /media/rescue-write-plan.json --root "$selected" --esp "$selected_esp" --journal /media/rescue-write-job --confirm "$confirmation"
    [ "$(cat "$selected/etc/repaired")" = repaired ] && [ "$before_mounts" = "$(cat /proc/self/mountinfo)" ] || fail rescue-write-cleanup
    check managed-chroot-explicit-write-and-cleanup
    printf '%s\n' '{"schema":1,"action":"shell","write":false,"network":false,"timeout_seconds":2,"shell_input":"/usr/bin/sleep 30 &\n/usr/bin/sleep 30\n"}' > /media/rescue-timeout.json
    call rescue-timeout-plan linux rescue-plan /media/rescue-timeout.json --root "$selected" --esp "$selected_esp" --output /media/rescue-timeout-plan.json
    confirmation=$(plan_digest /media/rescue-timeout-plan.json)
    reject rescue-timeout linux rescue-execute /media/rescue-timeout-plan.json --root "$selected" --esp "$selected_esp" --journal /media/rescue-timeout-job --confirm "$confirmation"
    [ "$before_mounts" = "$(cat /proc/self/mountinfo)" ] && [ ! -e /media/rescue-timeout-job/mount-root ] || fail rescue-timeout-cleanup
    check managed-chroot-timeout-reaps-processes-and-mounts
    call rescue-stale-plan linux rescue-plan /media/rescue-readonly.json --root "$selected" --esp "$selected_esp" --output /media/rescue-stale-plan.json
    confirmation=$(plan_digest /media/rescue-stale-plan.json)
    printf 'ID=fedora\nNAME="Synthetic Fedora shell root"\n' > "$selected/etc/os-release"
    reject rescue-stale linux rescue-execute /media/rescue-stale-plan.json --root "$selected" --esp "$selected_esp" --journal /media/rescue-stale-job --confirm "$confirmation"
    [ ! -e /media/rescue-stale-job ] || fail rescue-stale-journal
    call rescue-fedora-plan linux rescue-plan /media/rescue-readonly.json --root "$selected" --esp "$selected_esp" --output /media/rescue-fedora-plan.json
    confirmation=$(plan_digest /media/rescue-fedora-plan.json)
    call rescue-fedora linux rescue-execute /media/rescue-fedora-plan.json --root "$selected" --esp "$selected_esp" --journal /media/rescue-fedora-job --confirm "$confirmation"
    printf '%s\n' '{"schema":1,"action":"package-check","write":false,"network":false,"timeout_seconds":15}' > /media/rescue-package.json
    reject rescue-missing-distribution-tool linux rescue-plan /media/rescue-package.json --root "$selected" --esp "$selected_esp" --output /media/rescue-package-plan.json
    [ "$before_mounts" = "$(cat /proc/self/mountinfo)" ] || fail rescue-fedora-cleanup
    check managed-chroot-profile-change-and-missing-package-tool-refusals
    ;;
btrfs)
    while read -r module; do insmod "$module" || fail btrfs-module; done < /ure-function-modules
    mkdir -p /mnt/btrfs
    btrfs_disk=$(fixture_disk ure-functional-btrfs)
    mount -t btrfs -o subvolid=5 "$btrfs_disk" /mnt/btrfs
    root=/mnt/btrfs
    manage() {
        name=$1; request=$2
        printf '%s\n' "$request" > "/media/$name-request.json"
        call "$name-plan" btrfs plan "/media/$name-request.json" --root "$root" --profile vm-fixture --output "/media/$name-plan.json"
        confirmation=$(plan_digest "/media/$name-plan.json")
        call "$name-execute" btrfs execute "/media/$name-plan.json" --root "$root" --journal "/media/$name-job" --confirm "$confirmation"
    }
    manage btrfs-create '{"schema":1,"action":"create","path":"active"}'
    printf 'original payload\n' > "$root/active/payload"
    call btrfs-snapshot-plan btrfs snapshot-plan active . first --root "$root" --profile vm-fixture --output /media/snapshot-first
    confirmation=$(plan_digest /media/snapshot-first/plan.json)
    reject btrfs-snapshot-wrong-confirm btrfs snapshot-execute /media/snapshot-first --root "$root" --confirm wrong
    call btrfs-snapshot btrfs snapshot-execute /media/snapshot-first --root "$root" --confirm "$confirmation"
    cmp "$root/active/payload" "$root/first/payload" || fail btrfs-snapshot-bytes
    call btrfs-full-plan btrfs send-plan first --root "$root" --profile vm-fixture --output /media/send-first
    confirmation=$(plan_digest /media/send-first/plan.json)
    call btrfs-full-capture btrfs send-capture /media/send-first --root "$root" --confirm "$confirmation"
    call btrfs-full-verify btrfs send-verify /media/send-first
    call btrfs-stream-check btrfs stream-check /media/send-first/stream.bin
    cp /media/send-first/stream.bin /media/corrupt-stream.bin
    printf '\377' | dd of=/media/corrupt-stream.bin bs=1 seek=30 conv=notrunc 2>/dev/null
    reject btrfs-corrupt-stream btrfs stream-check /media/corrupt-stream.bin
    printf 'modified payload\n' > "$root/active/payload"
    call btrfs-second-plan btrfs snapshot-plan active . second --root "$root" --profile vm-fixture --output /media/snapshot-second
    confirmation=$(plan_digest /media/snapshot-second/plan.json)
    call btrfs-second btrfs snapshot-execute /media/snapshot-second --root "$root" --confirm "$confirmation"
    call btrfs-incremental-plan btrfs send-plan second first --root "$root" --profile vm-fixture --output /media/send-second
    confirmation=$(plan_digest /media/send-second/plan.json)
    call btrfs-incremental-capture btrfs send-capture /media/send-second --root "$root" --confirm "$confirmation"
    call btrfs-incremental-verify btrfs send-verify /media/send-second
    check btrfs-cli-readonly-snapshot-full-and-incremental-send
    manage btrfs-rollback '{"schema":1,"action":"rollback","path":"active","snapshot":"first","saved_path":"retained-original"}'
    cmp "$root/active/payload" "$root/first/payload" && cmp "$root/retained-original/payload" "$root/second/payload" || fail btrfs-retained-data
    check btrfs-cli-rollback-retains-both-versions
    for operation in subvolumes usage device-stats scrub-status balance-status; do call "btrfs-$operation" btrfs "$operation" --root "$root"; done
    manage btrfs-scrub '{"schema":1,"action":"scrub","device_id":1,"repair":true}'
    manage btrfs-balance '{"schema":1,"action":"balance","usage_percent":90,"chunk_limit":1}'
    manage btrfs-shrink '{"schema":1,"action":"resize","target_bytes":402653184}'
    manage btrfs-grow '{"schema":1,"action":"resize","target_bytes":536870912}'
    check btrfs-cli-scrub-bounded-balance-shrink-grow
    umount /mnt/btrfs
    ;;
*) fail unknown-group;;
esac
echo "URE_FUNCTION_EXIT $mode 0"
sync
umount /media
trap - EXIT
echo o > /proc/sysrq-trigger
toybox reboot -p -f
