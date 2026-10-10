#!/usr/bin/env bash
# Host-only inspection of the default boot script selected by pinned AOSP init.
touch_payload_init() {
    local root=${1:?Extracted ramdisk required} script
    root=$(realpath -e -- "$root") || return 1
    script="$root/system/etc/init/hw/init.rc"
    [[ -f $script && -s $script && ! -L $script ]] || return 1
    # Never resolve payload aliases into host files or a different payload path.
    [[ $(realpath -e -- "$script") == "$script" ]] || return 1
    [[ $(rg -c -x '    copy /system/etc/ld.config.txt /linkerconfig/ld.config.txt' "$script") == 1 ]] || return 1
    printf '%s\n' "$script"
}
