#!/usr/bin/env bash
# Execute the production Make hook against disposable host-only payloads.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source_path="$component/src/upstream/orangefox-android16/bootable/recovery"
bash "$component/scripts/prepare-recovery-patches.sh" "$source_path" check
scratch=$(mktemp -d "$component/build/recovery-packaging-hooks-XXXXXX")
awk '/^LOCAL_MODULE := file_contexts_text$/ {copy=1} copy {print} copy && /^include \$\(BUILD_PHONY_PACKAGE\)$/ {exit}' \
    "$source_path/Android.mk" > "$scratch/production-hook.mk"
[[ -s $scratch/production-hook.mk ]]
ncurses="$component/src/upstream/orangefox-android16/external/libncurses"
awk '/name: "libncurses-terminfo-x-xterm_recovery"/ {copy=1} copy {print} copy && /^}/ {exit}' \
    "$ncurses/Android.bp" > "$scratch/declared-terminfo-module.bp"
rg -q 'srcs: \["lib/terminfo/x/xterm\*"\]' "$scratch/declared-terminfo-module.bp"
rg -q 'recovery: true' "$scratch/declared-terminfo-module.bp"
[[ -s $ncurses/lib/terminfo/x/xterm-256color ]]
cases=0
run_case() {
    local nano=$1 terminfo=$2 vendor=$3 fault=$4
    local trial="$scratch/case-$nano-$terminfo-$vendor-$fault"
    local text=soong/.intermediates/system/sepolicy/file_contexts.concat.tmp/android_common/gen
    local binary=product/obj/ETC/file_contexts.bin_intermediates
    mkdir -p "$trial/$text" "$trial/$binary" "$trial/product/recovery/root/system/etc/terminfo"
    printf 'context-text\n' > "$trial/$text/file_contexts.concat.tmp"
    printf 'context-binary\n' > "$trial/$binary/file_contexts.bin"
    printf 'retained-before-hook\n' > "$trial/product/recovery/root/system/etc/terminfo/sentinel"
    if [[ $nano == 1 ]]; then
        mkdir -p "$trial/product/system/etc/nano"
        printf 'nano-config\n' > "$trial/product/system/etc/nano/nanorc"
    fi
    if [[ $terminfo == 1 ]]; then
        mkdir -p "$trial/external/libncurses/lib/terminfo/x"
        printf 'terminal-entry\n' > "$trial/external/libncurses/lib/terminfo/x/xterm-256color"
    fi
    if [[ $vendor == 1 ]]; then
        mkdir -p "$trial/product/root"
        ln -s /never-used "$trial/product/root/vendor"
    fi
    case $fault in
        none) :;;
        text) unlink "$trial/$text/file_contexts.concat.tmp";;
        binary) unlink "$trial/$binary/file_contexts.bin";;
        nano) unlink "$trial/product/system/etc/nano/nanorc"; rmdir "$trial/product/system/etc/nano";;
        *) exit 2;;
    esac
    cat > "$trial/Makefile" <<EOF
SOONG_OUT_DIR := soong
PRODUCT_OUT := product
TARGET_OUT_ETC := product/system/etc
TARGET_RECOVERY_ROOT_OUT := product/recovery/root
OF_USE_NANO_EDITOR := $nano
OF_MANUAL_ROOT_VENDOR_ERROR_FIX := $vendor
BUILD_PHONY_PACKAGE := /dev/null
include $scratch/production-hook.mk
.PHONY: all
all:
	@\$(LOCAL_POST_INSTALL_CMD)
EOF
    if make --no-print-directory -C "$trial" > "$trial/result.log" 2>&1; then
        [[ $fault == none ]] || { echo 'A failed packaging dependency was ignored' >&2; exit 1; }
        cmp "$trial/$text/file_contexts.concat.tmp" "$trial/product/recovery/root/file_contexts"
        cmp "$trial/$binary/file_contexts.bin" "$trial/product/recovery/root/file_contexts.bin"
        if [[ $nano == 1 ]]; then
            cmp "$trial/product/system/etc/nano/nanorc" "$trial/product/recovery/root/system/etc/nano/nanorc"
        fi
        # The ncurses Soong modules own terminal entries. The unrelated
        # file-context hook must leave concurrently installed entries intact.
        [[ $(cat "$trial/product/recovery/root/system/etc/terminfo/sentinel") == retained-before-hook ]]
        [[ ! -e $trial/product/recovery/root/system/etc/terminfo/x/xterm-256color ]]
        if [[ $vendor == 1 ]]; then [[ -d $trial/product/root/vendor && ! -L $trial/product/root/vendor ]]; fi
    else
        [[ $fault != none ]] || { cat "$trial/result.log" >&2; exit 1; }
        [[ $(cat "$trial/product/recovery/root/system/etc/terminfo/sentinel") == retained-before-hook ]]
        if [[ $vendor == 1 ]]; then [[ -L $trial/product/root/vendor ]]; fi
    fi
    ((cases+=1))
}
for nano in 0 1; do
    for terminfo in 0 1; do
        for vendor in 0 1; do run_case "$nano" "$terminfo" "$vendor" none; done
    done
done
for fault in text binary nano; do run_case 1 1 1 "$fault"; done
! rg -q 'cp.*libncurses/lib/terminfo|rm.*terminfo' "$scratch/production-hook.mk"
# Exercise a real concurrent installer: the hook may run beside a terminfo
# target, but must neither remove its directory nor substitute source files.
trial="$scratch/case-1-1-0-none"
mkdir -p "$trial/product/recovery/root/system/etc/terminfo/x"
(
    for ((i=0;i<100;i++)); do
        printf 'declared-install-target\n' > "$trial/product/recovery/root/system/etc/terminfo/x/xterm-256color"
    done
) & installer=$!
for ((i=0;i<10;i++)); do make --no-print-directory -C "$trial" >> "$trial/result.log" 2>&1; done
wait "$installer"
[[ $(cat "$trial/product/recovery/root/system/etc/terminfo/x/xterm-256color") == declared-install-target ]]
printf 'Production recovery packaging hook: %d cases passed; fixture retained privately.\n' "$cases"
