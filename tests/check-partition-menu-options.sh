#!/usr/bin/env bash
# Host-only menu admission checks; no mounted filesystem or block-device I/O.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
core="$component/src/upstream/orangefox-android16/bootable/recovery"
work=$(mktemp -d "$component/build/partition-menu-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
awk '/^struct PartitionList \{/ {copy=1} copy {print} copy && /^};/ {exit}' "$core/partitions.hpp" > "$work/list.inc"
awk '/if \(!ure::legacy_write_decision\(ure::LegacyWrite::Wipe\).allowed\)/ {copy=1} copy {print} copy && /^\t\tstruct PartitionList dalvik;/ {exit}' \
    "$core/partitionmanager.cpp" | sed '$d' > "$work/wipe.inc"
rg -q 'row.selectable = false;' "$work/wipe.inc"
rg -Uq 'void GUIPartitionList::NotifySelect\(size_t item_selected\)\n\{\n[[:space:]]*if \(item_selected < mList.size\(\) && !mList.at\(item_selected\).selectable\) return;' "$core/gui/partitionlist.cpp"
cat > "$work/test.cpp" <<'CPP'
#include "recovery_write_policy.hpp"
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include "list.inc"
struct Partition { bool Is_Present = true; };
std::map<std::string, Partition> present;
Partition* Find_Partition_By_Path(const std::string& path) { const auto it=present.find(path); return it==present.end() ? nullptr : &it->second; }
std::string gui_lookup(const std::string&, const std::string& fallback) { return fallback; }
void list(std::vector<PartitionList>* Partition_List) {
#include "wipe.inc"
}
int main() {
    for(const auto* path:{"/metadata","/data","/frp","/system"}) present[path]={};
    std::vector<PartitionList> rows; list(&rows); assert(rows.size()==7);
    for(const auto& row:rows) { assert(!row.selectable && !row.selected && !row.isFiles); assert(!row.Display_Name.empty()); }
    assert(rows[2].Mount_Point=="/cache" && rows[2].Display_Name.find("No physical partition")!=std::string::npos);
    assert(rows[5].Mount_Point=="/frp" && rows[5].Display_Name.find("Unavailable")!=std::string::npos);
    present["/cache"]={}; rows.clear(); list(&rows);
    assert(rows[2].Display_Name.find("Unavailable")!=std::string::npos);
    assert(ure::legacy_write_decision(ure::LegacyWrite::Wipe).allowed==false);
}
CPP
c++ -std=c++17 -Wall -Wextra -Werror -I"$work" -I"$component/src/device/xiaomi/uke/recoveryctl/libuke" "$work/test.cpp" -o "$work/test"
"$work/test"
fstab="$component/src/device/xiaomi/uke/recovery/root/system/etc/recovery.fstab"
for role in boot init_boot vendor_boot recovery dtbo vbmeta vbmeta_system bluetooth dsp modem; do
    for slot in a b; do
        label="${role}_${slot}"
        awk -v mount="/$label" -v block="/dev/block/bootdevice/by-name/$label" '
          $1==mount {n++; if($2!="emmc" || $3!=block || !index($0,"backup=1;") || !index($0,"flashimg=0;") || !index($0,"canbewiped=0;") || !index($0,"wipeingui=0")) exit 1}
          END {if(n!=1) exit 1}' "$fstab"
    done
done
for label in misc frp modemst1 modemst2 fsg fsc metadata_image persist_image; do
    [[ $(awk -v path="/$label" '$1==path {n++} END {print n+0}' "$fstab") == 1 ]]
done
[[ $(awk '$1=="/super" {n++} END {print n+0}' "$fstab") == 0 ]]
rg -q '^vendor[[:space:]]+/vendor[[:space:]]+ext4[[:space:]]+ro,noload,nosuid,nodev' "$fstab"
! rg -q '"/data;/boot;"' "$core/data.cpp" "$core/gui/theme/portrait_hdpi/resources/vars.xml"
printf '%s\n' 'Partition menus: explicit-slot backup flags, raw metadata/persist choices, vendor EXT4 no-replay fallback and unavailable Wipe rows passed; host-only source controls.'
