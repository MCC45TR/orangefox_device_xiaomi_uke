// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include "recovery_write_policy.hpp"
#include <algorithm>
#include <array>
#include <set>
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

namespace ure {
namespace {
// Firmware archives, donor declarations and property dumps are not accepted
// unit profiles. No environment variable, supplied JSON or GUI option adds one.
constexpr std::size_t accepted_live_profiles=0;
static_assert(!live_storage_backend_accepted(),"Review native unit-profile admission before enabling a live writer");
bool same(const Value& a,const Value& b) { return json(a)==json(b); }
#ifndef __ANDROID__
constexpr std::array identity_keys{"commercial_model","model_number","device","product","hardware_sku",
    "vendor_sku","firmware_version","build_fingerprint"};
constexpr std::array geometry_keys{"bytes","logical_sector_bytes","first_usable_lba","last_usable_lba","entry_count","entry_size"};
constexpr std::array partition_keys{"index","label","start_lba","end_lba","type_guid","attributes"};
void bounded(const Value& value,unsigned depth,std::size_t& nodes,std::size_t& bytes,const char* code) {
    require(depth<=10 && ++nodes<=32768,code,"Device-profile declarations exceed their structure budget");
    if(value.isString())bytes+=value.asString().size();
    else if(value.isArray())for(const auto& child:value)bounded(child,depth+1,nodes,bytes,code);
    else if(value.isObject())for(const auto& key:value.getMemberNames()) { bytes+=key.size(); bounded(value[key],depth+1,nodes,bytes,code); }
    require(bytes<=1024*1024,code,"Device-profile declarations exceed their string budget");
}
void fields(const Value& value,std::initializer_list<const char*> allowed,const char* code) {
    require(value.isObject(),code,"Device-profile declarations must be objects");
    std::set<std::string> keys; for(const auto* key:allowed)keys.insert(key);
    for(const auto& key:value.getMemberNames())require(keys.contains(key),code,"Unknown device-profile field: "+key);
    for(const auto* key:allowed)require(value.isMember(key),code,"Missing device-profile field: "+std::string(key));
}
bool text(const Value& value,std::size_t limit=256) { return value.isString() && !value.asString().empty() && value.asString().size()<=limit; }
void identity(const Value& value,const char* code) {
    fields(value,{"commercial_model","model_number","device","product","hardware_sku","vendor_sku","firmware_version","build_fingerprint"},code);
    for(const auto* key:identity_keys)require(text(value[key]),code,"An exact bounded commercial, SKU and firmware identity is required");
}
void geometry(const Value& row,bool observed,const char* code) {
    if(observed)fields(row,{"lun","bytes","logical_sector_bytes","first_usable_lba","last_usable_lba","entry_count","entry_size",
        "partitions","disk_guid","primary_gpt_sha256","backup_gpt_sha256","gpt_copies_matching"},code);
    else fields(row,{"lun","bytes","logical_sector_bytes","first_usable_lba","last_usable_lba","entry_count","entry_size","partitions"},code);
    require(row["lun"].isUInt() && row["lun"].asUInt()<6,code,"LUN numbers must be 0 through 5");
    for(const auto* key:geometry_keys)require(row[key].isUInt64(),code,"GPT geometry requires exact unsigned byte/LBA counts");
    const auto sector=row["logical_sector_bytes"].asUInt64(),bytes=row["bytes"].asUInt64(),count=row["entry_count"].asUInt64(),size=row["entry_size"].asUInt64();
    require((sector==512 || sector==4096) && bytes>=sector*128 && bytes<=1024ULL*1024*1024*1024 && bytes%sector==0 &&
        count>0 && count<=512 && size==128,code,"Unsupported, truncated or excessive LUN/GPT geometry");
    const auto table_sectors=(count*size+sector-1)/sector,first=row["first_usable_lba"].asUInt64(),last=row["last_usable_lba"].asUInt64();
    require(first>=2+table_sectors && first<=last && last<bytes/sector-1-table_sectors && row["partitions"].isArray() &&
        !row["partitions"].empty() && row["partitions"].size()<=count,code,"Complete GPT partition ranges must fit both metadata copies");
    std::set<std::string> labels,guids; std::set<std::uint64_t> indexes;
    std::vector<std::pair<std::uint64_t,std::uint64_t>> ranges;
    for(const auto& part:row["partitions"]) {
        if(observed)fields(part,{"index","label","start_lba","end_lba","type_guid","attributes","partuuid"},code);
        else fields(part,{"index","label","start_lba","end_lba","type_guid","attributes"},code);
        require(part["index"].isUInt64() && part["index"].asUInt64()>0 && part["index"].asUInt64()<=count &&
            indexes.insert(part["index"].asUInt64()).second && text(part["label"],72) && labels.insert(part["label"].asString()).second &&
            part["type_guid"].isString() && uuid(part["type_guid"].asString()) && part["attributes"].isUInt64() &&
            part["start_lba"].isUInt64() && part["end_lba"].isUInt64(),code,"Partition indexes, labels, GUIDs and attributes must be complete and unique");
        const auto begin=part["start_lba"].asUInt64(),end=part["end_lba"].asUInt64();
        require(begin>=first && end<=last && begin<=end,code,"Partition range lies outside the usable LUN"); ranges.emplace_back(begin,end);
        if(observed)require(part["partuuid"].isString() && uuid(part["partuuid"].asString()) && guids.insert(part["partuuid"].asString()).second,
            code,"Unit partition GUIDs must be nonzero and unique");
    }
    std::sort(ranges.begin(),ranges.end());
    for(std::size_t i=1;i<ranges.size();++i)require(ranges[i-1].second<ranges[i].first,code,"GPT partition ranges overlap");
    if(observed)require(row["disk_guid"].isString() && uuid(row["disk_guid"].asString()) && row["primary_gpt_sha256"].isString() &&
        hash_valid(row["primary_gpt_sha256"].asString()) && row["backup_gpt_sha256"].isString() && hash_valid(row["backup_gpt_sha256"].asString()) &&
        row["gpt_copies_matching"].isBool(),code,"Both complete GPT copies and unit disk identity are required");
}
Value partition_projection(const Value& parts,bool guids_only=false) {
    std::vector<Value> ordered; for(const auto& part:parts) {
        Value item;
        if(guids_only) { item["index"]=part["index"]; item["partuuid"]=part["partuuid"]; }
        else for(const auto* key:partition_keys)item[key]=part[key];
        ordered.push_back(std::move(item));
    }
    std::sort(ordered.begin(),ordered.end(),[](const Value& a,const Value& b) { return a["index"].asUInt64()<b["index"].asUInt64(); });
    Value out(Json::arrayValue); for(const auto& part:ordered)out.append(part); return out;
}
void check(Value& out,const char* code,bool matches,const std::string& detail) {
    Value row; row["code"]=code; row["matches"]=matches; row["detail"]=detail; out["checks"].append(row);
    if(!matches)out["blockers"].append(code);
}
#endif
std::string property(const char* key) {
#ifdef __ANDROID__
    std::array<char,PROP_VALUE_MAX> value{}; __system_property_get(key,value.data()); return value.data();
#else
    (void)key; return {};
#endif
}
} // namespace

Value device_profile_admission_status(const Root& system,const std::string& requested_profile) {
    require(identifier(requested_profile),"invalid-profile","Select an explicit installed firmware profile");
    Root current("/"); const bool authoritative=same(descriptor_identity(current.fd()),descriptor_identity(system.fd()));
    Value out; out["schema"]=1; out["format"]="ure-device-profile-admission"; out["requested_profile"]=requested_profile;
    out["accepted_live_profile_count"]=Json::UInt64(accepted_live_profiles); out["profile_accepted"]=false;
    out["live_plan_allowed"]=false; out["physical_test_record"]=false; out["private_record"]=true; out["current_system_root"]=authoritative;
    out["observations_are_installed_firmware_proof"]=false; out["geometry_state"]="NOT_COLLECTED_UNACCEPTED_UNIT_PROFILE";
    out["blockers"]=Value(Json::arrayValue);
    if(!authoritative)out["blockers"].append("profile-observation-root-untrusted");
    out["blockers"].append("device-profile-unaccepted"); out["blockers"].append("six-lun-geometry-unverified");
    out["blockers"].append("unit-gpt-backups-unverified"); out["blockers"].append("installed-firmware-unverified");
    out["property_origin"]="current-recovery-property-service; observations only";
    if(authoritative)for(const auto* key:{"ro.product.device","ro.product.name","ro.product.model","ro.boot.hardware.sku",
        "ro.boot.product.hardware.sku","ro.build.version.incremental","ro.build.fingerprint"})out["property_observations"][key]=property(key);
    out["required_evidence"]="Separate commercial model/SKU/capacity and installed firmware; native read-only six-LUN primary/backup GPT and complete ranges; this unit's original GUID backups; both whole-partition boot stacks and reviewed recovery fallback";
    out["reason"]="No live unit profile has been accepted. Firmware archive geometry, shared Uke codename, labels, imported JSON and mounted /data capacity cannot authorize a plan.";
    return out;
}

Value device_profile_compare_fixture(const Value& contract,const Value& observation) {
#ifdef __ANDROID__
    (void)contract; (void)observation;
    throw Error("fixture-only-command","Imported profile declarations cannot be compared as live device admission");
#else
    constexpr const char* bad_contract="invalid-profile-contract",*bad_observation="invalid-profile-evidence";
    std::size_t nodes=0,bytes=0; bounded(contract,0,nodes,bytes,bad_contract); nodes=bytes=0; bounded(observation,0,nodes,bytes,bad_observation);
    fields(contract,{"schema","format","profile","identity","luns","boot_assets"},bad_contract);
    fields(observation,{"schema","format","identity","unit_identity_sha256","boot_id_sha256","luns","unit_gpt_backups","boot_assets","boot_state"},bad_observation);
    require(contract["schema"]==1 && contract["format"]=="ure-device-profile-contract" && contract["profile"].isString() && identifier(contract["profile"].asString()),bad_contract,"Invalid fixture profile contract");
    require(observation["schema"]==1 && observation["format"]=="ure-device-profile-observation",bad_observation,"Invalid fixture profile observation");
    identity(contract["identity"],bad_contract); identity(observation["identity"],bad_observation);
    for(const auto* key:{"unit_identity_sha256","boot_id_sha256"})require(observation[key].isString() && hash_valid(observation[key].asString()) && observation[key].asString()!=sha256(""),bad_observation,"Missing unit or current-boot identity");
    require(contract["luns"].isArray() && contract["luns"].size()==6,bad_contract,"A contract requires all six LUNs");
    require(observation["luns"].isArray() && observation["luns"].size()==6 && observation["unit_gpt_backups"].isArray() && observation["unit_gpt_backups"].size()==6,
        bad_observation,"Read-only declarations require all six LUNs and their unit-bound original GPT backups");
    Value out; out["schema"]=1; out["format"]="ure-device-profile-comparison"; out["profile"]=contract["profile"];
    out["checks"]=Value(Json::arrayValue); out["blockers"]=Value(Json::arrayValue);
    check(out,"profile-identity-mismatch",same(contract["identity"],observation["identity"]),"Commercial model, model number, product, hardware/vendor SKU and exact installed firmware/fingerprint");
    std::set<std::string> disks,all_partuuids,all_labels; std::array<Value,6> tables;
    for(unsigned i=0;i<6;++i) {
        const auto& expected=contract["luns"][i]; const auto& actual=observation["luns"][i];
        geometry(expected,false,bad_contract); geometry(actual,true,bad_observation);
        require(expected["lun"].asUInt()==i,bad_contract,"Contract LUNs must be ordered 0 through 5");
        require(actual["lun"].asUInt()==i && disks.insert(actual["disk_guid"].asString()).second,bad_observation,"Observed LUNs must be ordered and have distinct disk GUIDs");
        for(const auto& part:actual["partitions"])require(all_partuuids.insert(part["partuuid"].asString()).second && all_labels.insert(part["label"].asString()).second,
            bad_observation,"Partition GUIDs and lookup labels must be unique across all six LUNs");
        bool matches=actual["gpt_copies_matching"]==true;
        for(const auto* key:geometry_keys)matches=matches && same(expected[key],actual[key]);
        matches=matches && same(partition_projection(expected["partitions"]),partition_projection(actual["partitions"]));
        check(out,"profile-lun-geometry-mismatch",matches,"Complete declared primary/backup GPT geometry and every partition of LUN "+std::to_string(i));
        const auto& backup=observation["unit_gpt_backups"][i];
        fields(backup,{"lun","unit_identity_sha256","disk_guid","bytes","logical_sector_bytes","primary_gpt_sha256","backup_gpt_sha256","partitions"},bad_observation);
        require(backup["lun"].isUInt() && backup["lun"].asUInt()==i && backup["unit_identity_sha256"].isString() && hash_valid(backup["unit_identity_sha256"].asString()) &&
            backup["disk_guid"].isString() && uuid(backup["disk_guid"].asString()) && backup["bytes"].isUInt64() && backup["logical_sector_bytes"].isUInt64() &&
            backup["primary_gpt_sha256"].isString() && hash_valid(backup["primary_gpt_sha256"].asString()) && backup["backup_gpt_sha256"].isString() && hash_valid(backup["backup_gpt_sha256"].asString()) &&
            backup["partitions"].isArray() && backup["partitions"].size()==actual["partitions"].size(),bad_observation,"Invalid unit GPT backup declaration");
        std::set<std::uint64_t> backup_indexes;
        for(const auto& part:backup["partitions"]) { fields(part,{"index","partuuid"},bad_observation);
            require(part["index"].isUInt64() && backup_indexes.insert(part["index"].asUInt64()).second && part["partuuid"].isString() && uuid(part["partuuid"].asString()),bad_observation,"Invalid original partition identity"); }
        bool bound=same(backup["unit_identity_sha256"],observation["unit_identity_sha256"]);
        for(const auto* key:{"disk_guid","bytes","logical_sector_bytes","primary_gpt_sha256","backup_gpt_sha256"})bound=bound && same(backup[key],actual[key]);
        bound=bound && same(partition_projection(backup["partitions"],true),partition_projection(actual["partitions"],true));
        check(out,"unit-gpt-backup-mismatch",bound,"This unit's original disk/partition GUIDs and both complete GPT records on LUN "+std::to_string(i)); tables[i]=actual;
    }
    for(const auto& disk:disks)require(!all_partuuids.contains(disk),bad_observation,"A disk GUID cannot also identify a partition");
    require(contract["boot_assets"].isArray() && contract["boot_assets"].size()==10,bad_contract,"Contract requires both complete boot/init_boot/vendor_boot/dtbo/recovery stacks");
    require(observation["boot_assets"].isArray() && observation["boot_assets"].size()==10,bad_observation,"Observation requires both complete boot stacks");
    std::set<std::string> expected_labels,actual_labels; bool boot_match=true;
    auto assets=[&](const Value& input,const char* code) {
        std::set<std::string> labels;
        for(const auto& asset:input) {
            fields(asset,{"lun","label","partition_bytes","sha256","checksum_scope"},code);
            require(asset["lun"].isUInt() && asset["lun"].asUInt()<6 && text(asset["label"],72) && asset["partition_bytes"].isUInt64() &&
                asset["partition_bytes"].asUInt64()>0 && asset["sha256"].isString() && hash_valid(asset["sha256"].asString()) && asset["checksum_scope"]=="whole-partition" &&
                labels.insert(asset["label"].asString()).second,code,"Only unique whole-partition boot declarations are supported");
        }
        return labels;
    };
    expected_labels=assets(contract["boot_assets"],bad_contract); actual_labels=assets(observation["boot_assets"],bad_observation);
    for(const auto* name:{"boot","init_boot","vendor_boot","dtbo","recovery"})for(const auto* suffix:{"_a","_b"})
        require(expected_labels.contains(std::string(name)+suffix),bad_contract,"Missing exact A/B boot label");
    boot_match=expected_labels==actual_labels;
    for(const auto& expected:contract["boot_assets"]) {
        const auto label=expected["label"].asString(); const auto lun=expected["lun"].asUInt();
        bool partition_found=false;
        for(const auto& part:tables[lun]["partitions"])if(part["label"]==label) {
            const auto capacity=(part["end_lba"].asUInt64()-part["start_lba"].asUInt64()+1)*tables[lun]["logical_sector_bytes"].asUInt64();
            partition_found=same(Value(Json::UInt64(capacity)),expected["partition_bytes"]);
        }
        bool matched=false; for(const auto& actual:observation["boot_assets"])if(actual["label"]==label)matched=same(actual,expected);
        boot_match=boot_match && partition_found && matched;
    }
    check(out,"boot-stack-mismatch",boot_match,"Both declared slots and recovery fallback use exact full-partition capacities and hashes, including programmed gaps/footers");
    const auto& state=observation["boot_state"];
    fields(state,{"slots","active_slot","snapshot_status","fallback_bootable","bootloader_unlocked"},bad_observation);
    require(state["slots"].isUInt() && state["active_slot"].isString() && state["snapshot_status"].isString() && state["fallback_bootable"].isBool() && state["bootloader_unlocked"].isBool(),bad_observation,"Malformed boot-control declarations");
    check(out,"profile-boot-state-unsafe",state["slots"]==2 && (state["active_slot"]=="_a" || state["active_slot"]=="_b") && state["snapshot_status"]=="none" &&
        state["fallback_bootable"]==true && state["bootloader_unlocked"]==true,"Exact two-slot state, idle Virtual A/B and declared stock fallback");
    out["declarations_match"]=out["blockers"].empty(); out["state"]=out["declarations_match"]==true ? "MATCHED_DECLARATIONS_ONLY" : "MISMATCH";
    out["comparison_basis"]="bounded supplied fixture declarations; no raw geometry, backup bytes, signatures or hardware measured";
    out["live_plan_allowed"]=false; out["profile_accepted"]=false; out["physical_test_record"]=false; out["private_record"]=true;
    out["accepted_live_profile_count"]=Json::UInt64(accepted_live_profiles); out["authority_blocker"]="fixture-only-no-live-authority"; return out;
#endif
}
} // namespace ure
