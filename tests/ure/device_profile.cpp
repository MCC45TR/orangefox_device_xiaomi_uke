// SPDX-License-Identifier: Apache-2.0
// Synthetic declarations only. No UFS, ADB, block open or tablet fixture.
#include "uke.h"
#include <algorithm>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
void check(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
std::string guid(unsigned value) {
    std::ostringstream out; out<<"00000000-0000-4000-8000-"<<std::setw(12)<<std::setfill('0')<<value; return out.str();
}
void rejected(const std::function<void()>& call,const char* code) {
    try { call(); } catch(const ure::Error& error) { check(error.code==code,"Wrong profile refusal code"); return; }
    throw std::runtime_error("Malformed profile declaration was accepted");
}
bool blocker(const ure::Value& result,const std::string& code) {
    return std::find(result["blockers"].begin(),result["blockers"].end(),ure::Value(code))!=result["blockers"].end();
}
std::pair<ure::Value,ure::Value> declarations() {
    ure::Value contract,observed; contract["schema"]=1; contract["format"]="ure-device-profile-contract"; contract["profile"]="fixture-poco-os2";
    auto& id=contract["identity"]; id["commercial_model"]="poco-pad-x1"; id["model_number"]="25099RP08G"; id["device"]="uke";
    id["product"]="uke_p_global"; id["hardware_sku"]="ukepgl"; id["vendor_sku"]="cliffs";
    id["firmware_version"]="OS2.0.205.0.VOZMIXM"; id["build_fingerprint"]="POCO/uke_p_global/uke:15/AQ3A.240801.002/OS2.0.205.0.VOZMIXM:user/release-keys";
    observed["schema"]=1; observed["format"]="ure-device-profile-observation"; observed["identity"]=id;
    observed["unit_identity_sha256"]=ure::sha256("synthetic-unit-one"); observed["boot_id_sha256"]=ure::sha256("synthetic-boot-one");
    contract["boot_assets"]=ure::Value(Json::arrayValue);
    for(unsigned i=0;i<6;++i) {
        ure::Value row; row["lun"]=i; row["bytes"]=Json::UInt64(32*1024*1024); row["logical_sector_bytes"]=4096;
        row["first_usable_lba"]=6; row["last_usable_lba"]=8186; row["entry_count"]=128; row["entry_size"]=128;
        row["partitions"]=ure::Value(Json::arrayValue);
        auto part=[&](const std::string& label,unsigned index) {
            ure::Value entry; entry["index"]=index; entry["label"]=label; entry["start_lba"]=256*index; entry["end_lba"]=256*index+127;
            entry["type_guid"]="0fc63daf-8483-4772-8e79-3d69d8477de4"; entry["attributes"]=Json::UInt64(0); row["partitions"].append(entry);
        };
        if(i==4) {
            unsigned index=1;
            for(const auto* name:{"boot","init_boot","vendor_boot","dtbo","recovery"})for(const auto* suffix:{"_a","_b"}) {
                const auto label=std::string(name)+suffix; part(label,index++);
                ure::Value asset; asset["lun"]=i; asset["label"]=label; asset["partition_bytes"]=Json::UInt64(128*4096);
                asset["sha256"]=ure::sha256("synthetic-whole-partition-"+label); asset["checksum_scope"]="whole-partition"; contract["boot_assets"].append(asset);
            }
        } else part("synthetic-protected-"+std::to_string(i),1);
        contract["luns"].append(row); row["disk_guid"]=guid(i+1); row["primary_gpt_sha256"]=ure::sha256("synthetic-primary-"+std::to_string(i));
        row["backup_gpt_sha256"]=ure::sha256("synthetic-backup-"+std::to_string(i)); row["gpt_copies_matching"]=true;
        for(auto& entry:row["partitions"])entry["partuuid"]=guid(100+i*100+entry["index"].asUInt());
        observed["luns"].append(row);
        ure::Value backup;
        for(const auto* key:{"lun","bytes","logical_sector_bytes","disk_guid","primary_gpt_sha256","backup_gpt_sha256"})backup[key]=row[key];
        backup["unit_identity_sha256"]=observed["unit_identity_sha256"]; backup["partitions"]=ure::Value(Json::arrayValue);
        for(const auto& entry:row["partitions"]) { ure::Value identity; identity["index"]=entry["index"]; identity["partuuid"]=entry["partuuid"]; backup["partitions"].append(identity); }
        observed["unit_gpt_backups"].append(backup);
    }
    observed["boot_assets"]=contract["boot_assets"]; auto& state=observed["boot_state"];
    state["slots"]=2; state["active_slot"]="_a"; state["snapshot_status"]="none"; state["fallback_bootable"]=true; state["bootloader_unlocked"]=true;
    return {contract,observed};
}
} // namespace
int main() {
    try {
        auto [contract,observed]=declarations();
        // Parse both records to exercise mixed native/parsed numeric types.
        const auto pristine=ure::device_profile_compare_fixture(ure::parse_json(ure::json(contract)),ure::parse_json(ure::json(observed)));
        check(pristine["declarations_match"]==true,"Matching fixture declarations were rejected");
        check(pristine["live_plan_allowed"]==false && pristine["profile_accepted"]==false &&
            pristine["accepted_live_profile_count"].asUInt64()==0 && pristine["physical_test_record"]==false,"Matching fixture declarations enabled hardware admission");
        ure::Root current("/"); const auto closed=ure::device_profile_admission_status(current,"global-os3.0.303.0");
        check(closed["profile_accepted"]==false && closed["live_plan_allowed"]==false && closed["accepted_live_profile_count"].asUInt64()==0 &&
            closed["observations_are_installed_firmware_proof"]==false,"Current-root properties enabled installed-unit acceptance");
        auto mismatch=[&](const std::function<void(ure::Value&)>& alter,const char* code) {
            auto value=observed; alter(value); const auto result=ure::device_profile_compare_fixture(contract,value);
            check(result["declarations_match"]==false && result["live_plan_allowed"]==false && blocker(result,code),"Mismatching evidence was accepted");
        };
        for(const auto* key:{"commercial_model","model_number","product","hardware_sku","vendor_sku","firmware_version","build_fingerprint"})
            mismatch([&](auto& value) { value["identity"][key]="other-model-or-firmware"; },"profile-identity-mismatch");
        for(unsigned i=0;i<6;++i) {
            mismatch([&](auto& value) { value["luns"][i]["bytes"]=Json::UInt64(64*1024*1024); },"profile-lun-geometry-mismatch");
            mismatch([&](auto& value) { value["luns"][i]["gpt_copies_matching"]=false; },"profile-lun-geometry-mismatch");
            mismatch([&](auto& value) { value["unit_gpt_backups"][i]["unit_identity_sha256"]=ure::sha256("other-unit"); },"unit-gpt-backup-mismatch");
            mismatch([&](auto& value) { value["unit_gpt_backups"][i]["disk_guid"]=guid(999); },"unit-gpt-backup-mismatch");
            mismatch([&](auto& value) { value["unit_gpt_backups"][i]["partitions"][0]["partuuid"]=guid(999); },"unit-gpt-backup-mismatch");
            mismatch([&](auto& value) { value["luns"][i]["partitions"][0]["attributes"]=Json::UInt64(1); },"profile-lun-geometry-mismatch");
            mismatch([&](auto& value) { value["luns"][i]["partitions"][0]["start_lba"]=257; },"profile-lun-geometry-mismatch");
        }
        for(unsigned i=0;i<10;++i)mismatch([&](auto& value) { value["boot_assets"][i]["sha256"]=ure::sha256("damaged-gap-or-footer"); },"boot-stack-mismatch");
        for(const auto* status:{"unknown","merging","snapshotted","cancelled",""})mismatch([&](auto& value) { value["boot_state"]["snapshot_status"]=status; },"profile-boot-state-unsafe");
        mismatch([](auto& value) { value["boot_state"]["fallback_bootable"]=false; },"profile-boot-state-unsafe");
        mismatch([](auto& value) { value["boot_state"]["bootloader_unlocked"]=false; },"profile-boot-state-unsafe");
        auto malformed=[&](const std::function<void(ure::Value&)>& alter) {
            auto value=observed; alter(value); rejected([&] { ure::device_profile_compare_fixture(contract,value); },"invalid-profile-evidence");
        };
        malformed([](auto& value) { value["luns"].resize(5); });
        malformed([](auto& value) { value["luns"][1]=value["luns"][0]; });
        malformed([](auto& value) { value["luns"][1]["disk_guid"]=value["luns"][0]["disk_guid"]; });
        malformed([](auto& value) { value["luns"][1]["partitions"][0]["partuuid"]=value["luns"][0]["partitions"][0]["partuuid"]; });
        malformed([](auto& value) { value["luns"][1]["partitions"][0]["partuuid"]=value["luns"][0]["disk_guid"]; });
        malformed([](auto& value) { value["luns"][0]["bytes"]=Json::UInt64(UINT64_MAX); });
        malformed([](auto& value) { value["luns"][0]["logical_sector_bytes"]=5120; });
        malformed([](auto& value) { value["luns"][0]["partitions"][0]["start_lba"]=1; });
        malformed([](auto& value) { value["luns"][4]["partitions"][1]["start_lba"]=300; });
        malformed([](auto& value) { value["luns"][0]["partitions"][0]["end_lba"]=Json::UInt64(UINT64_MAX); });
        malformed([](auto& value) { value["luns"][0]["partitions"][0]["attributes"]=-1; });
        malformed([](auto& value) { value["boot_assets"][0]["checksum_scope"]="oem-prefix"; });
        malformed([](auto& value) { value["boot_assets"].resize(9); });
        malformed([](auto& value) { value["boot_assets"][1]=value["boot_assets"][0]; });
        malformed([](auto& value) { value["identity"]["model_number"]=ure::Value(); });
        malformed([](auto& value) { value["unit_identity_sha256"]=ure::sha256(""); });
        malformed([](auto& value) { value["live_plan_allowed"]=true; });
        auto bad=contract; bad["luns"][0]["partitions"][0]["start_lba"]=0;
        rejected([&] { ure::device_profile_compare_fixture(bad,observed); },"invalid-profile-contract");
        bad=contract; bad["identity"]["model_number"]=std::string(1024*1024+1,'X');
        rejected([&] { ure::device_profile_compare_fixture(bad,observed); },"invalid-profile-contract");
        // GPT and boot declaration order cannot hide a partition; indexes and
        // labels remain exact, even when the arrays are reordered.
        auto reordered=observed; std::reverse(reordered["luns"][4]["partitions"].begin(),reordered["luns"][4]["partitions"].end());
        std::reverse(reordered["boot_assets"].begin(),reordered["boot_assets"].end());
        check(ure::device_profile_compare_fixture(contract,reordered)["declarations_match"]==true,"Equivalent exact declarations failed after reordering");
        std::cout<<"PASS exact six-LUN/model/SKU/firmware declarations, unit GUID backups, both boot stacks, malformed/foreign/missing evidence and always-closed device authority; synthetic records only\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
