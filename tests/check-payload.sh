#!/usr/bin/env bash
# Scan the complete staged ramdisk, not only the image header or executable names.
set -euo pipefail
payload=${1:?Usage: check-payload.sh RAMDISK_ROOT}
[[ -d $payload && ! -L $payload ]] || exit 2
failed=0
while IFS= read -r -d '' file; do
    relative=${file#"$payload"/}
    case "$relative" in
        *.py|*.pyc|*.pyo|*/python|*/python[0-9]*|*/libpython*|*/site-packages/*)
            printf 'Forbidden Python payload: %s\n' "$relative" >&2; failed=1;;
    esac
    if head -c 256 -- "$file" | LC_ALL=C grep -aE '^#!.*(python|pypy)' >/dev/null; then
        printf 'Renamed Python launcher in payload: %s\n' "$relative" >&2; failed=1
    fi
    if LC_ALL=C strings -a "$file" | grep -E '^Py_Initialize(Ex)?$|^Py_Main$|^PYTHONHOME$|^PYTHONPATH$' >/dev/null; then
        printf 'Python runtime identity in payload: %s\n' "$relative" >&2; failed=1
    fi
    if LC_ALL=C strings -a "$file" | grep -E '/home/|/root/|/run/user/|/var/home/|/Users/' >/dev/null; then
        printf 'Private absolute path in payload: %s\n' "$relative" >&2; failed=1
    fi
done < <(find "$payload" -type f -print0)
while IFS= read -r -d '' link; do
    relative=${link#"$payload"/}
    case "$relative" in
        *.py|*.pyc|*.pyo|*/python|*/python[0-9]*|*/libpython*|*/site-packages/*)
            printf 'Forbidden Python link: %s\n' "$relative" >&2; failed=1;;
    esac
    case $(readlink -- "$link") in
        /home/*|/root/*|/run/user/*|/var/home/*|/Users/*)
            printf 'Private target in payload link: %s\n' "$relative" >&2; failed=1;;
    esac
done < <(find "$payload" -type l -print0)
if [[ $failed != 0 ]]; then exit 1; fi
echo 'Full ramdisk privacy and no-Python scan passed.'
