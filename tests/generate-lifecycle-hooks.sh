#!/usr/bin/env bash
# Compile complete production lifecycle entry points with host-only effect mocks.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:?Generated C++ output is required}
recovery="$component/src/upstream/orangefox-android16/bootable/recovery"
fastboot="$component/src/upstream/orangefox-android16/system/core/fastboot"
temporary=$(mktemp -- "$output.tmp.XXXXXX")
trap 'rm -f -- "$temporary"' EXIT
extract() {
    local source=$1 signature=$2
    awk -v signature="$signature" '
      index($0, signature)==1 { if (seen++) exit 4; copying=1 }
      copying { print; if ($0=="}") { copying=0; complete++ } }
      END { if (seen!=1 || complete!=1 || copying) exit 5 }
    ' "$source"
}
{
    printf '%s\n' '#include "lifecycle_hooks.hpp"' \
        '#define sync(...) ure_test_sync(__VA_ARGS__)' \
        '#define umount2(...) ure_test_umount2(__VA_ARGS__)' \
        '#define sleep(...) ure_test_sleep(__VA_ARGS__)' \
        '#define usleep(...) ure_test_usleep(__VA_ARGS__)' \
        '#define pause(...) ure_test_pause(__VA_ARGS__)'
    extract "$recovery/partition.cpp" 'bool TWPartition::UnMount('
    extract "$recovery/partitionmanager.cpp" 'int TWPartitionManager::UnMount_By_Path('
    extract "$recovery/gui/action.cpp" 'int GUIAction::reboot(std::string arg)'
    # The syscall name also occurs in GUIAction::reboot, so intercept it only
    # after that method has been emitted without rewriting its declaration.
    printf '%s\n' '#define reboot(...) ure_test_reboot(__VA_ARGS__)' \
        '#define android_reboot(...) ure_test_android_reboot(__VA_ARGS__)'
    extract "$recovery/twrp-functions.cpp" 'int TWFunc::tw_reboot(RebootCommand command)'
    extract "$fastboot/device/commands.cpp" 'bool ShutDownHandler('
    extract "$fastboot/device/commands.cpp" 'bool RebootHandler('
    extract "$fastboot/device/commands.cpp" 'bool RebootBootloaderHandler('
    extract "$fastboot/device/commands.cpp" 'bool RebootFastbootHandler('
    extract "$fastboot/device/commands.cpp" 'bool RebootRecoveryHandler('
    extract "$recovery/recovery_utils/roots.cpp" 'int ensure_path_unmounted(const std::string& path) {'
    printf '%s\n' '#undef sync' '#undef umount2' '#undef sleep' '#undef usleep' '#undef pause' \
        '#undef reboot' '#undef android_reboot'
} > "$temporary"
mv -f -- "$temporary" "$output"
