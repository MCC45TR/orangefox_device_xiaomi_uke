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
        '#define chmod(...) ure_test_chmod(__VA_ARGS__)' \
        '#define unlink(...) ure_test_unlink(__VA_ARGS__)'
    extract "$recovery/partitionmanager.cpp" 'int TWPartitionManager::Format_Data(void) {'
    extract "$recovery/partitionmanager.cpp" 'void TWPartitionManager::Set_Active_Slot(const string& Slot) {'
    extract "$recovery/partition.cpp" 'void TWPartition::Change_Mount_Read_Only(bool new_value) {'
    extract "$recovery/partition.cpp" 'bool TWPartition::Mount(bool Display_Error) {'
    extract "$recovery/partition.cpp" 'bool TWPartition::Bind_Mount(bool Display_Error, const ure::LegacyLifecycleGuard* parent) {'
    extract "$recovery/openrecoveryscript.cpp" 'int OpenRecoveryScript::copy_script_file(string filename) {'
    extract "$recovery/openrecoveryscript.cpp" 'int OpenRecoveryScript::Run_ORS_File(const std::string& filename) {'
    extract "$recovery/gui/action.cpp" 'int GUIAction::dd(std::string arg)'
    extract "$recovery/gui/action.cpp" 'int GUIAction::cmd(std::string arg)'
    extract "$recovery/gui/action.cpp" 'int GUIAction::ftls(std::string arg)'
    extract "$recovery/twrpRepacker.cpp" 'bool twrpRepacker::Flash_Current_Twrp() {'
    extract "$recovery/twrp-functions.cpp" 'int TWFunc::stream_adb_backup(string &Restore_Name) {'
    extract "$fastboot/device/fastboot_device.cpp" 'void FastbootDevice::ExecuteCommands() {'
    extract "$fastboot/device/utility.cpp" 'bool OpenPartition(FastbootDevice* device, const std::string& name, PartitionHandle* handle,'
    extract "$fastboot/device/variables.cpp" 'bool GetPartitionSize(FastbootDevice* device, const std::vector<std::string>& args,'
    printf '%s\n' '#undef mount' '#undef statvfs' '#undef chmod' '#undef unlink'
} > "$output"
