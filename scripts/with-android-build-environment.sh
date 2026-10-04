#!/usr/bin/env bash
# Share the owned disk scratch with the inner Android build namespace.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tree=${1:?Android source directory required}
shift
[[ ${UKE_HOST_BUDGET_ACTIVE:-0} == 1 && $# -gt 0 && -d $tree && ! -L $tree ]]
tree=$(realpath -e -- "$tree")
bash "$component/scripts/host-temp-policy.sh" arm64 /tmp /tmp/host-temp-policy.json > /tmp/android-temp-before.json
mkdir -p "$tree/out-public/ccache"
bwrap --ro-bind / / --dev-bind /dev /dev --proc /proc --bind /tmp /tmp \
    --bind "$tree" /mnt --chdir /mnt \
    --ro-bind "$component/scripts/host-ccache.sh" /mnt/uke-host-ccache.sh \
    --ro-bind "$component/scripts/host-temp-policy.sh" /mnt/uke-host-temp-policy.sh \
    --ro-bind "$component/scripts/run-android-build-job.sh" /mnt/uke-run-android-build-job.sh \
    --setenv TMPDIR /tmp --setenv TMP /tmp --setenv TEMP /tmp \
    --setenv BUILD_USERNAME uke-builder --setenv BUILD_HOSTNAME uke-build \
    --setenv FOX_BUILD_BASH 1 --setenv OUT_DIR /mnt/out-public \
    --setenv FOX_LOCAL_CALLBACK_SCRIPT /mnt/device/xiaomi/uke/prepare-public-ramdisk.sh \
    --setenv XDG_CACHE_HOME /mnt/out-public/cache \
    --setenv USE_CCACHE 1 --setenv CCACHE_EXEC /usr/bin/ccache \
    --setenv CCACHE_DIR /mnt/out-public/ccache --setenv CCACHE_MAXSIZE 10G \
    --setenv CC_WRAPPER /mnt/uke-host-ccache.sh --setenv CXX_WRAPPER /mnt/uke-host-ccache.sh \
    bash /mnt/uke-run-android-build-job.sh "$@"
