#!/usr/bin/env bash
# Inspect every ELF and resolve NEEDED/interpreter paths inside the ramdisk.
set -euo pipefail
payload=$(realpath -- "${1:?Usage: check-elf-closure.sh RAMDISK_ROOT}")
count=0
while IFS= read -r -d '' file; do
    [[ $(head -c 4 -- "$file" | od -An -tx1 | tr -d ' \n') == 7f454c46 ]] || continue
    count=$((count+1))
    readelf -h "$file" | grep -q 'Machine:.*AArch64'
    readelf -h "$file" | grep -q 'Class:.*ELF64'
    interpreter=$(readelf -l "$file" | sed -n 's/.*Requesting program interpreter: \([^]]*\)].*/\1/p')
    if [[ -n $interpreter ]]; then
        case "$interpreter" in
            /system/bin/linker64) [[ -f $payload$interpreter && ! -L $payload$interpreter ]];;
            /system/bin/bootstrap/linker64)
                [[ $(readlink -- "$payload$interpreter") == ../linker64 && -f $payload/system/bin/linker64 ]];;
            *) printf 'Unresolved ELF interpreter for %s: %s\n' "${file#"$payload"/}" "$interpreter" >&2; exit 1;;
        esac
    fi
    while IFS= read -r needed; do
        [[ $needed != */* && $needed != *..* ]]
        dependency=
        for directory in system/lib64 vendor/lib64 sbin; do
            if [[ -f $payload/$directory/$needed && ! -L $payload/$directory/$needed ]]; then dependency="$payload/$directory/$needed"; break; fi
        done
        [[ -n $dependency ]] || { printf 'Missing ELF dependency for %s: %s\n' "${file#"$payload"/}" "$needed" >&2; exit 1; }
        readelf -h "$dependency" | grep -q 'Machine:.*AArch64'
    done < <(readelf -d "$file" | sed -n 's/.*(NEEDED).*\[\([^]]*\)\].*/\1/p')
done < <(find "$payload" -type f -print0)
((count>0))
printf 'ELF AArch64 architecture, interpreter and dependency closure passed: %s files.\n' "$count"
