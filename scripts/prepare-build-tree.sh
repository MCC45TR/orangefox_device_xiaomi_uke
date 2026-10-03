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
copy_changed() { cmp -s -- "$1" "$2" || cp -- "$1" "$2"; }

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

# Allow only the project recovery package to use the existing pinned Zstd codec.
codec_patch="$component/patches/0009-native-boot-audit-codecs.patch"
if git -C "$tree/external/zstd" apply --reverse --check "$codec_patch" 2>/dev/null; then
  :
elif git -C "$tree/external/zstd" apply --check "$codec_patch"; then
  git -C "$tree/external/zstd" apply "$codec_patch"
else
  echo 'Unexpected Zstd source visibility; refusing an unverified codec adapter' >&2; exit 1
fi

# Extract an immutable source snapshot into the active build tree. Do not run
# reference scripts. Only the project-owned Android adapter is staged with it.
wim_reference="$component/referances/upstream/wimlib"
wim_pin=cd5e231c348c255ae5088873b5a66ee0eb96fa07
wim_target="$tree/external/ure-wimlib"
[[ $(git -C "$wim_reference" rev-parse HEAD) == "$wim_pin" && -z $(git -C "$wim_reference" status --porcelain --untracked-files=no) ]]
if [[ ! -d $wim_target ]]; then
  mkdir -p "$wim_target"
  git -C "$wim_reference" archive "$wim_pin" | tar -xf - -C "$wim_target"
  printf '%s\n' "$wim_pin" > "$wim_target/.uke-linux-owned"
fi
[[ $(cat "$wim_target/.uke-linux-owned") == "$wim_pin" ]]
copy_changed "$component/configs/ure/wimlib-Android.bp" "$wim_target/Android.bp"
copy_changed "$component/configs/ure/wimlib-config.h" "$wim_target/config.h"

dropbear_reference="$component/referances/upstream/dropbear"
dropbear_pin=179de98f7b9584a309ffc48e39c61da940760740
dropbear_target="$tree/external/ure-dropbear"
[[ $(git -C "$dropbear_reference" rev-parse HEAD) == "$dropbear_pin" && -z $(git -C "$dropbear_reference" status --porcelain --untracked-files=no) ]]
if [[ ! -d $dropbear_target ]]; then
  mkdir -p "$dropbear_target"
  git -C "$dropbear_reference" archive "$dropbear_pin" | tar -xf - -C "$dropbear_target"
  printf '%s\n' "$dropbear_pin" > "$dropbear_target/.uke-linux-owned"
fi
[[ $(cat "$dropbear_target/.uke-linux-owned") == "$dropbear_pin" ]]
copy_changed "$component/configs/ure/dropbear-Android.bp" "$dropbear_target/Android.bp"
copy_changed "$component/configs/ure/dropbear-config.h" "$dropbear_target/config.h"
copy_changed "$component/configs/ure/dropbear-localoptions.h" "$dropbear_target/localoptions.h"
# The pinned defaults contain no multiline macro definitions. Match upstream's
# documented guard transform without executing the reference wrapper script.
if grep -E '^ *#define .*\\$' "$dropbear_target/src/default_options.h" >/dev/null; then
  echo 'Unexpected multiline Dropbear default macro' >&2; exit 1
fi
awk '/^ *#define / { print "#ifndef " $2; print; print "#endif"; next } { print }' \
  "$dropbear_target/src/default_options.h" > "$component/build/dropbear-options-guard.h"
copy_changed "$component/build/dropbear-options-guard.h" "$dropbear_target/default_options_guard.h"

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

# Apply the reviewed adapter patch only to the active, pinned source checkout.
# The reference archive remains unmodified and is never used as executable code.
recovery_source="$tree/bootable/recovery"
ui_patch="$component/patches/0002-native-ure-ui.patch"
if git -C "$recovery_source" apply --unidiff-zero --reverse --check "$ui_patch" 2>/dev/null; then
  :
elif git -C "$recovery_source" apply --unidiff-zero --check "$ui_patch"; then
  git -C "$recovery_source" apply --unidiff-zero "$ui_patch"
else
  echo 'Unexpected OrangeFox GUI source; refusing an unverified adapter patch' >&2
  exit 1
fi
cp -- "$device_source/ure-gui.cpp" "$recovery_source/gui/ure.cpp"
display_patch="$component/patches/0006-tablet-interface-density.patch"
monitor_patch="$component/patches/0007-usb-monitor-and-input.patch"
monitor_present=false
if git -C "$recovery_source" apply --reverse --check "$monitor_patch" 2>/dev/null; then monitor_present=true; fi
if $monitor_present; then
  : # The stacked patch changes older context; verify the complete stack below.
elif git -C "$recovery_source" apply --reverse --check "$display_patch" 2>/dev/null; then
  :
elif git -C "$recovery_source" apply --check "$display_patch"; then
  git -C "$recovery_source" apply "$display_patch"
else
  echo 'Unexpected OrangeFox density hooks; refusing an unverified patch' >&2
  exit 1
fi
if git -C "$recovery_source" apply --reverse --check "$monitor_patch" 2>/dev/null; then
  :
elif git -C "$recovery_source" apply --check "$monitor_patch"; then
  git -C "$recovery_source" apply "$monitor_patch"
else
  echo 'Unexpected OrangeFox display/input hooks; refusing an unverified patch' >&2
  exit 1
fi
for file in display-mirror.hpp display-mirror.cpp display-mirror-layout.cpp; do
  copy_changed "$device_source/$file" "$recovery_source/minuitwrp/$file"
done
link_patch="$component/patches/0004-link-native-ure.patch"
if git -C "$recovery_source" apply --unidiff-zero --reverse --check "$link_patch" 2>/dev/null; then
  :
elif git -C "$recovery_source" apply --unidiff-zero --check "$link_patch"; then
  git -C "$recovery_source" apply --unidiff-zero "$link_patch"
else
  echo 'Unexpected OrangeFox link configuration; refusing adapter patch' >&2
  exit 1
fi
layout_patch="$component/patches/0008-partition-layout-graph.patch"
if git -C "$recovery_source" apply --reverse --check "$layout_patch" 2>/dev/null; then
  :
elif git -C "$recovery_source" apply --check "$layout_patch"; then
  git -C "$recovery_source" apply "$layout_patch"
else
  echo 'Unexpected OrangeFox partition graph hooks; refusing an unverified patch' >&2
  exit 1
fi
drm_patch="$component/patches/0010-drm-framebuffer-initialization.patch"
if git -C "$recovery_source" apply --reverse --check "$drm_patch" 2>/dev/null; then
  :
elif git -C "$recovery_source" apply --check "$drm_patch"; then
  git -C "$recovery_source" apply "$drm_patch"
else
  echo 'Unexpected DRM allocation source; refusing an unverified patch' >&2; exit 1
fi
# Reconstruct the complete reviewed stack in an isolated index. Later patches
# may change an earlier patch's context; compare exact final file bytes rather
# than weakening its context check or accepting unknown active-tree changes.
verification_index=$(mktemp "$component/build/recovery-patch-index-XXXXXX")
unlink "$verification_index"
trap '[[ ! -e $verification_index ]] || unlink "$verification_index"' EXIT
GIT_INDEX_FILE="$verification_index" git -C "$recovery_source" read-tree HEAD
for file in 0002-native-ure-ui.patch 0004-link-native-ure.patch 0006-tablet-interface-density.patch 0007-usb-monitor-and-input.patch 0008-partition-layout-graph.patch 0010-drm-framebuffer-initialization.patch 0011-literal-ure-theme-defaults.patch 0012-responsive-stock-theme.patch 0014-preserve-hid-report-boundaries.patch 0015-menu-list-default-scroll.patch; do
  GIT_INDEX_FILE="$verification_index" git -C "$recovery_source" apply --cached --unidiff-zero "$component/patches/$file"
literal_patch="$component/patches/0011-literal-ure-theme-defaults.patch"
if git -C "$recovery_source" apply --reverse --check "$literal_patch" 2>/dev/null; then
  :
elif git -C "$recovery_source" apply --check "$literal_patch"; then
  git -C "$recovery_source" apply "$literal_patch"
else
  echo 'Unexpected theme variable loader; refusing an unverified patch' >&2; exit 1
fi
responsive_patch="$component/patches/0012-responsive-stock-theme.patch"
if git -C "$recovery_source" apply --reverse --check "$responsive_patch" 2>/dev/null; then
  :
elif git -C "$recovery_source" apply --check "$responsive_patch"; then
  git -C "$recovery_source" apply "$responsive_patch"
else
  echo 'Unexpected stock theme geometry; refusing an unverified patch' >&2; exit 1
fi
hid_patch="$component/patches/0014-preserve-hid-report-boundaries.patch"
if git -C "$recovery_source" apply --reverse --check "$hid_patch" 2>/dev/null; then
  :
elif git -C "$recovery_source" apply --check "$hid_patch"; then
  git -C "$recovery_source" apply "$hid_patch"
else
  echo 'Unexpected HID report handling; refusing an unverified patch' >&2; exit 1
fi
menu_patch="$component/patches/0015-menu-list-default-scroll.patch"
if git -C "$recovery_source" apply --reverse --check "$menu_patch" 2>/dev/null; then
  :
elif git -C "$recovery_source" apply --check "$menu_patch"; then
  git -C "$recovery_source" apply "$menu_patch"
else
  echo 'Unexpected action-menu selection source; refusing an unverified patch' >&2; exit 1
fi
done
cmp <(GIT_INDEX_FILE="$verification_index" git -C "$recovery_source" diff --cached --name-only HEAD) \
    <(git -C "$recovery_source" diff --name-only HEAD)
while IFS= read -r file; do
  cmp <(GIT_INDEX_FILE="$verification_index" git -C "$recovery_source" show ":$file") "$recovery_source/$file"
done < <(GIT_INDEX_FILE="$verification_index" git -C "$recovery_source" diff --cached --name-only HEAD)
unlink "$verification_index"
trap - EXIT
ntfs_source="$tree/external/ntfs-3g"
ntfs_patch="$component/patches/0003-build-ntfsresize.patch"
if git -C "$ntfs_source" apply --reverse --check "$ntfs_patch" 2>/dev/null; then
  :
elif git -C "$ntfs_source" apply --check "$ntfs_patch"; then
  git -C "$ntfs_source" apply "$ntfs_patch"
else
  echo 'Unexpected NTFS source; refusing an unverified build patch' >&2
  exit 1
fi

vendor_source="$tree/vendor/recovery"
callback_patch="$component/patches/0005-propagate-callback-failure.patch"
if git -C "$vendor_source" apply --unidiff-zero --reverse --check "$callback_patch" 2>/dev/null; then
  :
elif git -C "$vendor_source" apply --unidiff-zero --check "$callback_patch"; then
  git -C "$vendor_source" apply --unidiff-zero "$callback_patch"
else
  echo 'Unexpected OrangeFox packaging source; refusing callback patch' >&2
  exit 1
fi

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
