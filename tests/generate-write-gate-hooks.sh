#!/usr/bin/env bash
# Host/VM fixtures compile complete native entry points, never copied guards.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:?Generated C++ output is required}
recovery="$component/src/upstream/orangefox-android16/bootable/recovery"
fastboot="$component/src/upstream/orangefox-android16/system/core/fastboot"
extract() {
    local source=$1 signature=$2
    awk -v signature="$signature" '
      index($0, signature)==1 { if (seen++) exit 4; copying=1 }
      copying { print; if ($0=="}") { copying=0; complete++ } }
      END { if (seen!=1 || complete!=1 || copying) exit 5 }
    ' "$source"
}
{
    printf '%s\n' '#include "write-gate-hooks.h"' \
        '#define mount(...) ure_test_mount(__VA_ARGS__)' \
        '#define statvfs(...) ure_test_statvfs(__VA_ARGS__)' \
        '#define chmod(...) ure_test_chmod(__VA_ARGS__)'
    extract "$recovery/partitionmanager.cpp" 'int TWPartitionManager::Format_Data(void) {'
    extract "$recovery/partitionmanager.cpp" 'void TWPartitionManager::Set_Active_Slot(const string& Slot) {'
    extract "$recovery/partition.cpp" 'void TWPartition::Change_Mount_Read_Only(bool new_value) {'
    extract "$recovery/partition.cpp" 'bool TWPartition::Mount(bool Display_Error) {'
    extract "$fastboot/device/fastboot_device.cpp" 'void FastbootDevice::ExecuteCommands() {'
    extract "$fastboot/device/utility.cpp" 'bool OpenPartition(FastbootDevice* device, const std::string& name, PartitionHandle* handle,'
    printf '%s\n' '#undef mount' '#undef statvfs' '#undef chmod'
} > "$output"
