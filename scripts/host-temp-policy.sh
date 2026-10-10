#!/usr/bin/env bash
# Host-only disk scratch admission and identity checks across mount namespaces.
set -euo pipefail
umask 077
export LC_ALL=C
scratch_mode() {
    [[ $1 =~ ^[a-z][a-z0-9-]{0,40}$ ]]
    if [[ $1 == arm64-incremental ]]; then printf '%s\n' arm64; else printf '%s\n' "$1"; fi
}
capacity() {
    local mode=$1 fs=$2 block=$3 available=$4 total_inodes=$5 free_inodes=$6 value
    mode=$(scratch_mode "$mode")
    [[ $mode =~ ^[a-z][a-z0-9-]{0,40}$ ]]
    case $fs in ext2|ext3|ext4|ext2/3/4|xfs|btrfs|f2fs) :;;
        *) echo 'Host scratch must use a reviewed local disk filesystem.' >&2; return 1;;
    esac
    for value in "$block" "$available" "$total_inodes" "$free_inodes"; do
        [[ $value =~ ^(0|[1-9][0-9]*)$ && ${#value} -le 15 ]] || return 1
    done
    jq -en --arg mode "$mode" --arg fs "$fs" --argjson block "$block" --argjson available "$available" \
        --argjson total "$total_inodes" --argjson free "$free_inodes" '
      (if $mode=="arm64" then 8589934592 else 2147483648 end) as $minimum |
      ($fs=="btrfs" and $total==0 and $free==0) as $dynamic |
      if $block<512 or $block>1048576 or ($block*$available)>9007199254740991 or
         ($block*$available)<$minimum or (if $dynamic then false else ($total<1 or $free>$total or $free<131072) end)
      then error("Host scratch capacity or inode accounting is insufficient")
      else {minimum_bytes:$minimum,available_bytes:($block*$available),
            inode_accounting:(if $dynamic then "dynamic-btrfs" else "fixed" end),
            total_inodes:$total,free_inodes:$free,minimum_fixed_free_inodes:131072} end'
}
if [[ ${1:-} == --capacity ]]; then
    [[ $# == 7 ]]
    shift
    capacity "$@"
    exit 0
fi
mode=$(scratch_mode "${1:?Usage: host-temp-policy.sh MODE DIRECTORY [EXPECTED_RECEIPT]}")
directory=${2:?Private scratch directory required}
expected=${3:-}
[[ $# == 2 || $# == 3 ]]
[[ -d $directory && ! -L $directory ]]
directory=$(realpath -e -- "$directory")
read -r device inode owner permissions < <(stat -c '%d %i %u %a' -- "$directory")
[[ $owner == "$UID" && $permissions == 700 ]]
read -r block available total_inodes free_inodes fsid magic < <(stat -fc '%S %a %c %d %i %t' -- "$directory")
# Query the resolved path, not the first matching mountinfo entry. A bind can
# shadow another mount at the same target and mount-list order is not identity.
case $magic in
    ef53) fs=ext2/3/4;;
    58465342) fs=xfs;;
    9123683e) fs=btrfs;;
    f2f52010) fs=f2fs;;
    *) echo 'Host scratch must use a reviewed local disk filesystem.' >&2; exit 1;;
esac
summary=$(capacity "$mode" "$fs" "$block" "$available" "$total_inodes" "$free_inodes")
identity=$(jq -cn --argjson device "$device" --argjson inode "$inode" --argjson owner "$owner" \
    --arg fs "$fs" --arg fsid "$fsid" --arg magic "$magic" \
    '{device_id:$device,directory_inode:$inode,owner_uid:$owner,mode:700,filesystem_type:$fs,filesystem_id:$fsid,filesystem_magic:$magic}')
if [[ -n $expected ]]; then
    [[ -f $expected && ! -L $expected ]]
    [[ $(stat -c '%u %a %h' -- "$expected") == "$UID 600 1" ]]
    jq -e --arg mode "$mode" --argjson identity "$identity" \
        '.schema_version==1 and .job_mode==$mode and .identity==$identity' "$expected" >/dev/null || {
        echo 'Host scratch identity changed across isolation; heavy work refused.' >&2; exit 1;
    }
fi
# The snapshot is not an allocation guarantee, especially for Btrfs metadata.
# Exercise a small exclusive create/write/fsync/unlink before admitting work.
probe=$(mktemp -- "$directory/.scratch-admission-XXXXXXXX")
trap 'rm -f -- "$probe"' EXIT
printf 'disk-scratch-admission\n' > "$probe"
sync -f "$probe"
rm -- "$probe"
trap - EXIT
jq -cn --arg mode "$mode" --argjson identity "$identity" --argjson capacity "$summary" \
    '{schema_version:1,job_mode:$mode,identity:$identity,capacity:$capacity,create_write_sync_unlink:true}'
