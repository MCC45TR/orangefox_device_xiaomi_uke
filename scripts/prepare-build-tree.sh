#!/usr/bin/env bash
# Prepare the ignored OrangeFox checkout from a verified stock firmware profile.
set -euo pipefail

component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
profile=${1:?Usage: prepare-build-tree.sh PROFILE_ID}
tree="$component/src/upstream/orangefox-android16"
device_source="$component/src/device/xiaomi/uke"
device_target="$tree/device/xiaomi/uke"
firmware_manifest="$component/manifests/firmware.lock.json"
build_make="$tree/build/make"
vendor_directory_patch="$component/patches/0001-preserve-recovery-vendor-directory.patch"

entry=$(jq -ce --arg id "$profile" '.profiles[] | select(.id==$id and .download_status=="verified")' "$firmware_manifest") || {
  echo 'Unknown or unverified firmware profile' >&2
  exit 2
}
[[ -d $tree/.repo && -f $tree/build/envsetup.sh ]] || { echo 'OrangeFox source sync is incomplete' >&2; exit 1; }
[[ -d $device_source && ! -L $device_source ]] || { echo 'Project device source is unavailable' >&2; exit 1; }
[[ -f $vendor_directory_patch ]] || { echo 'Recovery vendor directory patch is missing' >&2; exit 1; }
if git -C "$build_make" apply --unidiff-zero --reverse --check "$vendor_directory_patch" 2>/dev/null; then
  : # Already applied.
elif git -C "$build_make" apply --unidiff-zero --check "$vendor_directory_patch"; then
  git -C "$build_make" apply --unidiff-zero "$vendor_directory_patch"
else
  echo 'Unexpected build/make source; refusing to stage an unverified patch' >&2
  exit 1
fi

region=$(jq -r .region <<<"$entry" | tr '[:upper:]' '[:lower:]')
archive_name=$(jq -r .url <<<"$entry")
archive_name=${archive_name##*/}
archive="$component/referances/firmware/$region/raw/$archive_name"
expected_sha=$(jq -r .sha256 <<<"$entry")
[[ -f $archive && $(sha256sum "$archive" | cut -d' ' -f1) == "$expected_sha" ]] || {
  echo 'Firmware archive checksum does not match the lock' >&2
  exit 1
}

derived="$component/referances/firmware/$region/derived"
boot=$(find "$derived" -type f -path '*/images/boot.img' -print -quit)
[[ -n $boot ]] || { echo 'Run extract-boot-reference.sh first' >&2; exit 1; }

if [[ -e $device_target && ! -f $device_target/.uke-linux-owned ]]; then
  echo 'Refusing to replace an unmanaged Android device tree' >&2
  exit 1
fi
mkdir -p "$device_target"
cp -a "$device_source/." "$device_target/"
: > "$device_target/.uke-linux-owned"

stage="$component/build/stock-$region/boot"
mkdir -p "$stage" "$device_target/prebuilt" "$component/reports/private"
unpack_bootimg --boot_img "$boot" --out "$stage" >/dev/null
[[ -s $stage/kernel ]] || { echo 'Stock boot image did not yield a kernel' >&2; exit 1; }
cp -- "$stage/kernel" "$device_target/prebuilt/kernel"

kernel_sha=$(sha256sum "$stage/kernel" | cut -d' ' -f1)
jq -n --arg profile "$profile" --arg source "$expected_sha" --arg kernel "$kernel_sha" \
  '{profile:$profile,source_archive_sha256:$source,stock_kernel_sha256:$kernel,device_tree:"src/device/xiaomi/uke",target:"device/xiaomi/uke"}' \
  > "$component/reports/private/$profile-build-inputs.json"

printf '%s prepared from verified profile %s; stock kernel sha256 %s\n' "$device_target" "$profile" "$kernel_sha"
