#!/usr/bin/env bash
# Share the owned disk scratch with the inner Android build namespace.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tree=${1:?Android source directory required}
output=${2:?Fresh owned Android output directory required}
shift 2
[[ ${UKE_HOST_BUDGET_ACTIVE:-0} == 1 && $# -gt 0 && -d $tree && ! -L $tree && -d $output && ! -L $output ]]
tree=$(realpath -e -- "$tree")
output=$(realpath -e -- "$output")
bash "$component/scripts/host-temp-policy.sh" arm64 /tmp /tmp/host-temp-policy.json > /tmp/android-temp-before.json
[[ $(stat -c '%u %a' "$output") == "$UID 700" ]]
mkdir -p "$component/build/ccache/android" "$output/ccache" /tmp/uke-build-launchers /tmp/uke-build-home
bwrap --ro-bind / / --dev-bind /dev /dev --proc /proc --bind /tmp /tmp \
    --ro-bind "$tree" /mnt --bind "$output" /mnt/out-public --chdir /mnt \
    --bind "$component/build/ccache/android" /mnt/out-public/ccache \
    --ro-bind "$component/scripts/host-ccache.sh" /tmp/uke-build-launchers/host-ccache.sh \
    --ro-bind "$component/scripts/host-temp-policy.sh" /tmp/uke-build-launchers/host-temp-policy.sh \
    --ro-bind "$component/scripts/run-android-build-job.sh" /tmp/uke-build-launchers/run-android-build-job.sh \
    --clearenv --setenv PATH /usr/bin:/bin --setenv LC_ALL C --setenv LANG C --setenv TZ UTC \
    --setenv HOME /tmp/uke-build-home --setenv USER uke-builder --setenv LOGNAME uke-builder \
    --setenv SOURCE_DATE_EPOCH 1790726400 --setenv BUILD_DATETIME 1790726400 --setenv BUILD_NUMBER uke-r12 \
    --setenv PYTHONDONTWRITEBYTECODE 1 \
    --setenv UKE_HOST_BUDGET_ACTIVE 1 --setenv UKE_HOST_JOBS "$UKE_HOST_JOBS" \
    --setenv GOMAXPROCS "$GOMAXPROCS" --setenv GOMEMLIMIT "$GOMEMLIMIT" --setenv GOGC "$GOGC" \
    --setenv TMPDIR /tmp --setenv TMP /tmp --setenv TEMP /tmp \
    --setenv BUILD_USERNAME uke-builder --setenv BUILD_HOSTNAME uke-build \
    --setenv FOX_BUILD_BASH 1 --setenv OUT_DIR /mnt/out-public \
    --setenv FOX_LOCAL_CALLBACK_SCRIPT /mnt/device/xiaomi/uke/prepare-public-ramdisk.sh \
    --setenv XDG_CACHE_HOME /mnt/out-public/cache \
    --setenv USE_CCACHE 1 --setenv CCACHE_EXEC /usr/bin/ccache \
    --setenv CCACHE_DIR /mnt/out-public/ccache --setenv CCACHE_MAXSIZE 10G \
    --setenv CC_WRAPPER /tmp/uke-build-launchers/host-ccache.sh --setenv CXX_WRAPPER /tmp/uke-build-launchers/host-ccache.sh \
    bash /tmp/uke-build-launchers/run-android-build-job.sh "$@"
