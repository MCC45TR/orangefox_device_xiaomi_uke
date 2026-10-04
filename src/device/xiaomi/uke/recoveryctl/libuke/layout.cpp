// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include "recovery_write_policy.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <set>
#include <sstream>

namespace ure {
namespace {
constexpr std::uint64_t mib=1048576, precision=1000000;
__extension__ typedef unsigned __int128 Wide;
struct Role { const char* id; const char* label; const char* type; std::vector<std::string> filesystems; };
const std::array<Role,4> roles{{
    {"esp","uke_esp","c12a7328-f81f-11d2-ba4b-00a0c93ec93b",{"fat32"}},
    {"linux","uke_linux","0fc63daf-8483-4772-8e79-3d69d8477de4",{"ext4","btrfs","f2fs"}},
    {"windows","uke_windows","ebd0a0a2-b9e5-4433-87c0-68b6b72699c7",{"ntfs"}},
    {"userdata","userdata","",{"ext4","f2fs"}}
}};
const Role& role(const std::string& id) {
    const auto it=std::find_if(roles.begin(),roles.end(),[&](const Role& r) { return id==r.id; });
    require(it!=roles.end(),"invalid-layout-role","Select ESP, Linux, Windows or userdata"); return *it;
}
bool userdata(const Value& part) { return part["label"]=="userdata"; }
void keys(const Value& value,const std::set<std::string>& allowed) {
    require(value.isObject(),"invalid-layout-request","Expected a layout object");
    for(const auto& name:value.getMemberNames())require(allowed.contains(name),"invalid-layout-request","Unknown layout field: "+name);
}
std::uint64_t decimal(const std::string& text) {
    require(!text.empty() && text.size()<=24,"invalid-layout-size","Enter a decimal size with at most six decimal places");
    std::uint64_t whole=0,fraction=0,scale=precision; bool dot=false; unsigned places=0;
    for(char c:text) {
        if((c=='.' || c==',') && !dot) { dot=true; continue; }
        require(c>='0' && c<='9',"invalid-layout-size","Use decimal digits; signs, grouping, spaces and exponents are not accepted");
        const auto digit=static_cast<unsigned>(c-'0');
        if(dot) { require(++places<=6,"invalid-layout-size","At most six decimal places are accepted"); scale/=10; fraction+=digit*scale; }
        else { require(whole<=(UINT64_MAX/precision-digit)/10,"layout-size-overflow","Requested size is too large"); whole=whole*10+digit; }
    }
    require(text.front()!='.' && text.front()!=',' && (!dot || places>0),"invalid-layout-size","A decimal requires digits on both sides");
    require(whole<=(UINT64_MAX-fraction)/precision,"layout-size-overflow","Requested size is too large"); return whole*precision+fraction;
}
std::uint64_t scaled(std::uint64_t value,std::uint64_t multiplier,std::uint64_t divisor) {
    const auto result=static_cast<Wide>(value)*multiplier/divisor;
    require(result<=static_cast<Wide>(INT64_MAX),"layout-size-overflow","Requested byte count exceeds signed storage offsets"); return static_cast<std::uint64_t>(result);
}
std::string new_uuid() {
    auto id=operation_id(); id[12]='4'; id[16]="89ab"[static_cast<unsigned>(id[16])%4];
    return id.substr(0,8)+"-"+id.substr(8,4)+"-"+id.substr(12,4)+"-"+id.substr(16,4)+"-"+id.substr(20);
}
std::string normalized_uuid(const Value& value) {
    require(value.isString() && uuid(value.asString()),"invalid-layout-guid","Select a nonzero GUID in canonical UUID notation");
    auto id=value.asString(); for(auto& byte:id)byte=static_cast<char>(std::tolower(static_cast<unsigned char>(byte))); return id;
}
std::string seal(Value value,const char* key) { value.removeMember(key); return sha256(json(value)); }
std::string display_size(std::uint64_t bytes) {
    const auto hundredths=scaled(bytes,100,mib); return std::to_string(hundredths/100)+"."+(hundredths%100<10 ? "0" : "")+std::to_string(hundredths%100)+" MiB";
}
void put(std::string& data,std::size_t offset,std::uint64_t value,unsigned bytes) {
    require(offset<=data.size() && bytes<=data.size()-offset,"invalid-layout","GPT field exceeds its metadata buffer");
    for(unsigned i=0;i<bytes;++i)data[offset+i]=static_cast<char>((value>>(i*8))&255);
}
std::uint32_t crc(std::string_view data) {
    std::uint32_t out=UINT32_MAX;
    for(char byte:data) { out^=static_cast<unsigned char>(byte); for(unsigned bit=0;bit<8;++bit)out=(out>>1)^((out&1U) ? 0xedb88320U : 0U); } return out^UINT32_MAX;
}
std::string guid_bytes(const std::string& id) {
    require(uuid(id),"invalid-layout","A nonzero partition GUID is required"); std::string plain;
    for(char c:id)if(c!='-')plain+=c;
    auto nibble=[](char c) { return static_cast<unsigned>(c<='9' ? c-'0' : (c>='a' ? c-'a' : c-'A')+10); };
    constexpr std::array<unsigned,16> order{3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15}; std::string out(16,'\0');
    for(unsigned i=0;i<16;++i)out[order[i]]=static_cast<char>((nibble(plain[i*2])<<4)|nibble(plain[i*2+1]));
    return out;
}
} // namespace

Value partition_live_blockers() {
    // Enabling a device writer requires a reviewed change to this capability
    // contract as well as profile, ownership and durability admission.
    static_assert(!live_storage_backend_accepted(),"Review partition capability admission before enabling live writes");
    Value out(Json::arrayValue);
    struct Blocker { const char* code; const char* reason; };
    constexpr std::array blockers{
        Blocker{"live-storage-writer-unaccepted","No native device writer is accepted for this release"},
        Blocker{"device-profile-unaccepted","Commercial model, SKU, capacity and installed firmware need exact accepted unit evidence"},
        Blocker{"six-lun-geometry-unverified","Complete measured six-LUN GPT geometry and same-unit original backups are required"},
        Blocker{"installed-firmware-unverified","Package source pins and Android property declarations do not establish installed firmware trust"},
        Blocker{"android-fbe-trust-unverified","KeyMint, TEE and installed encryption policy are required before credential use, mapping or encrypted data operations"},
        Blocker{"virtual-ab-state-unverified","Known inactive merge and reviewed super/logical partition ownership are required"},
        Blocker{"exclusive-storage-ownership-unverified","Accepted device-wide operation ownership, unmounted targets and snapshot exclusion are required"},
        Blocker{"physical-fallback-unverified","An independently rehearsed stock recovery route and verified original backups are required"},
        Blocker{"forced-restart-durability-unverified","Host SIGKILL does not establish tablet forced-restart recovery or durable writes"}
    };
    for(const auto& blocker:blockers) { Value item; item["code"]=blocker.code; item["reason"]=blocker.reason; out.append(item); }
    return out;
}
Value partition_capabilities() {
    Value out; out["schema"]=1; out["format"]="ure-partition-capabilities"; out["read_only"]=true; out["physical_test_record"]=false;
    auto& image=out["regular_image"]; image["source_implemented"]=true; image["target_validation_required"]=true;
    image["layout_preview"]=true; image["gpt_metadata_transaction"]=true; image["filesystem_and_gpt_job"]=true;
    image["allocation_pool"]="ORIGINAL_USERDATA_ONLY"; image["minimum_userdata_bytes"]=Json::UInt64(32*1024*1024);
    image["maximum_userdata_bytes"]=Json::UInt64(512ULL*1024*1024*1024);
    image["preserve_userdata_filesystems"]=Value(Json::arrayValue); image["preserve_userdata_filesystems"].append("ext4"); image["preserve_userdata_filesystems"].append("f2fs");
    image["encrypted_userdata_preservation"]=false; image["encrypted_userdata_reason_code"]="userdata-encryption-unverified";
    image["before_userdata_data_migration"]=false; image["before_userdata_reason_code"]="userdata-migration-required";
    image["before_userdata_recreate"]=true; image["before_userdata_required_mode"]="advanced"; image["before_userdata_required_policy"]="recreate";
    image["before_userdata_destroys_original_data"]=true; image["existing_shared_esp_policy"]="PRESERVE_EXACT_BYTES";
    image["existing_shared_esp_migration"]=false; image["existing_os_partition_reason_code"]="existing-os-partition";
    image["new_esp_allocation_to_retain_existing"]="zero"; image["advanced_non_userdata_content_jobs"]=false;
    image["advanced_guid_edits_scope"]="EXPLICIT_GPT_METADATA_ONLY"; image["android_userdata_boot_compatibility_verified"]=false;
    image["filesystem_tools"]=filesystem_capabilities()["filesystems"];
    image["six_lun_stock_restore"]=true; image["stock_source_profiles"]=Value(Json::arrayValue); image["stock_source_profiles"].append("global-os3.0.303.0");
    image["firmware_and_model_tags"]="DECLARATIONS_ONLY"; image["forced_restart_evidence"]="HOST_PROCESS_SIGKILL_ONLY";
    auto& live=out["live_device"]; live["repartition_available"]=false; live["userdata_shrink_available"]=false; live["userdata_recreate_available"]=false;
    live["before_userdata_data_migration_available"]=false; live["six_lun_stock_restore_available"]=false; live["advanced_mode_bypasses_admission"]=false;
    live["blockers"]=partition_live_blockers(); live["imported_declarations_authorize_writes"]=false;
    live["credential_use_allowed"]=false; live["mapper_creation_allowed"]=false; live["encrypted_mount_allowed"]=false;
    return out;
}

std::uint64_t layout_size_bytes(const std::string& amount,const std::string& unit,std::uint64_t pool) {
    require(pool<=INT64_MAX,"invalid-size","Layout pool exceeds supported storage offsets"); const auto quantity=decimal(amount);
    if(unit=="%") { require(quantity<=100*precision,"invalid-layout-percent","Percentage must be between zero and 100"); return scaled(pool,quantity,100*precision); }
    const auto factor=unit=="GB" ? 1000000000ULL : unit=="GiB" ? 1073741824ULL : unit=="MiB" ? mib : 0;
    require(factor>0,"invalid-layout-unit","Select GB, GiB, MiB or %"); return scaled(quantity,factor,precision);
}
Value partition_layout(const StorageTarget& target,const Value& input,const std::string& profile,const Root* system) {
    require(identifier(profile),"invalid-profile","Select a firmware profile"); storage_revalidate(target,system);
    require(target.identity["kind"]=="regular-image" || target.identity["partition"]==false,"invalid-target","Layouts select a whole disk or LUN");
    const auto sector=target.identity["logical_sector_bytes"].asUInt(); const auto table=gpt_inspect(target.descriptor.get(),sector);
    require(table["healthy"]==true,"unhealthy-layout-source","Two valid agreeing GPT copies are required before layout design");
    keys(input,{"schema","format","mode","placement","userdata_policy","rows","record_edits"});
    require(input["schema"]==1 && input["format"]=="ure-layout-request" && input["rows"].isArray() && input["rows"].size()==roles.size(),
        "invalid-layout-request","Explicitly specify all four roles; zero size disables a new OS role");
    for(const auto* field:{"mode","placement","userdata_policy"})require(!input.isMember(field) || input[field].isString(),"invalid-layout-request","Layout options must be strings");
    const auto mode=input.get("mode","standard").asString(),placement=input.get("placement","after_userdata").asString(),policy=input.get("userdata_policy","preserve").asString();
    require(mode=="standard" || mode=="advanced","invalid-layout-mode","Select standard or advanced mode"); const bool advanced=mode=="advanced";
    require(placement=="after_userdata" || placement=="before_userdata","invalid-layout-placement","Place new OS partitions before or after userdata");
    require(policy=="preserve" || policy=="recreate","invalid-userdata-policy","Select verified shrink or erase and recreate userdata");
    require(policy=="preserve" || advanced,"advanced-mode-required","Erasing and recreating userdata requires advanced mode");
    require(placement!="before_userdata" || (advanced && policy=="recreate"),"userdata-migration-required",
        "Placing partitions before userdata changes its start. Select advanced erase/recreate; preserving encrypted data requires an unavailable verified migration backend");
    Value original,protected_parts(Json::arrayValue); std::set<unsigned> occupied; std::set<std::string> ids;
    for(const auto& part:table["partitions"]) {
        occupied.insert(part["index"].asUInt()); ids.insert(part["partuuid"].asString());
        if(userdata(part)) { require(original.isNull(),"ambiguous-user-area","Exactly one userdata record is required"); original=part;
            require(part["attributes"].isUInt64() && part["attributes"].asUInt64()==0,"protected-partition","Attributed userdata requires a separately verified ownership workflow"); }
        else protected_parts.append(part);
    }
    for(const auto& part:table["reserved_records"]) { occupied.insert(part["index"].asUInt()); ids.insert(part["partuuid"].asString()); protected_parts.append(part); }
    require(!original.isNull(),"user-area-unavailable","Existing userdata is required; free GPT gaps and other partitions are never added to its allocation pool");
    const auto start=original["start_lba"].asUInt64()*sector,raw_end=(original["end_lba"].asUInt64()+1)*sector,finish=raw_end/mib*mib;
    require(start%mib==0,"unaligned-userdata","Preserving userdata requires its original start to be MiB aligned; this planner never rounds its start forward");
    require(start<finish,"insufficient-layout-space","Userdata has no complete 1 MiB allocation unit"); const auto pool=finish-start;
    Value request=input,rows(Json::arrayValue); std::map<std::string,Value> selected; std::map<std::string,std::uint64_t> requested,allocated;
    std::uint64_t used=0; bool remainder=false;
    for(const auto& row:input["rows"]) {
        keys(row,{"role","size","unit","filesystem","partuuid"});
        require(row["role"].isString() && selected.emplace(row["role"].asString(),row).second && row["size"].isString() && row["unit"].isString() && row["filesystem"].isString(),
            "invalid-layout-request","Layout roles must be unique and sizes/filesystems must be explicit strings");
        const auto role_id=row["role"].asString(); const auto& r=role(role_id);
        require(std::find(r.filesystems.begin(),r.filesystems.end(),row["filesystem"].asString())!=r.filesystems.end(),"invalid-layout-filesystem","Filesystem is incompatible with the selected role");
        if(row["unit"]=="remaining") {
            require(role_id=="userdata" && row["size"].asString().empty() && !remainder,"invalid-layout-remainder","Only userdata can receive remaining space, with an empty size"); remainder=true; continue;
        }
        requested[role_id]=layout_size_bytes(row["size"].asString(),row["unit"].asString(),pool); allocated[role_id]=requested.at(role_id)/mib*mib;
        require(requested.at(role_id)==0 || allocated.at(role_id)>0,"layout-size-too-small","An enabled role needs at least 1 MiB after alignment");
        require(allocated.at(role_id)<=pool-used,"insufficient-layout-space","Requested allocations exceed the original userdata capacity"); used+=allocated.at(role_id);
    }
    require(selected.size()==roles.size(),"invalid-layout-request","All four roles must be selected once");
    if(remainder) { requested["userdata"]=pool-used; allocated["userdata"]=pool-used; }
    require(allocated.at("userdata")>0,"userdata-required","Userdata cannot be deleted by this layout workflow");
    for(const auto& part:protected_parts)for(const auto& r:roles)if(r.id!=std::string("userdata") && part["label"]==r.label && allocated.at(r.id)>0)
        throw Error("existing-os-partition","An existing OS partition is protected. This workflow creates new roles only from userdata; back up and select a separate migration workflow");
    // A request that leaves every OS role disabled must not trim userdata merely
    // because its existing end is not MiB aligned.
    const bool preserve_full=remainder && allocated.at("esp")==0 && allocated.at("linux")==0 && allocated.at("windows")==0;
    const auto end=preserve_full ? raw_end : finish,graph_pool=end-start;
    if(preserve_full) { requested["userdata"]=graph_pool; allocated["userdata"]=graph_pool; }
    std::array<std::string,4> order=placement=="after_userdata" ? std::array<std::string,4>{"userdata","esp","linux","windows"} : std::array<std::string,4>{"esp","linux","windows","userdata"};
    request["mode"]=mode; request["placement"]=placement; request["userdata_policy"]=policy; request["rows"]=Value(Json::arrayValue);
    std::uint64_t cursor=start;
    for(const auto& role_id:order) {
        auto row=selected.at(role_id); const auto& r=role(role_id); const auto bytes=allocated.at(role_id),amount=requested.at(role_id); const auto old=role_id=="userdata" ? original : Value(); Value result;
        if(row.isMember("partuuid"))row["partuuid"]=normalized_uuid(row["partuuid"]);
        result["role"]=r.id; result["label"]=r.label; result["filesystem"]=row["filesystem"]; result["requested_size"]=row["size"]; result["unit"]=row["unit"];
        result["requested_bytes"]=Json::UInt64(amount); result["bytes"]=Json::UInt64(bytes); result["rounding_bytes"]=Json::UInt64(amount-bytes);
        result["previous"]=old; result["enabled"]=bytes!=0; result["offset"]=Json::UInt64(cursor); result["pool_offset"]=Json::UInt64(cursor-start);
        result["action"]=old.isNull() ? (bytes==0 ? "ABSENT" : "CREATE") : "UNCHANGED_GEOMETRY";
        if(bytes>0) {
            std::string id;
            if(!old.isNull()) {
                id=old["partuuid"].asString();
                if(row.isMember("partuuid") && row["partuuid"]!=id) {
                    require(advanced,"advanced-mode-required","Changing an existing GUID requires advanced mode");
                    require(row["partuuid"].isString() && uuid(row["partuuid"].asString()) && ids.insert(row["partuuid"].asString()).second,"invalid-layout-guid","New partition GUID is zero or duplicated");
                    id=row["partuuid"].asString();
                }
            } else { require(!row.isMember("partuuid") || row["partuuid"].isString(),"invalid-layout-guid","A GUID must be a string"); id=row.isMember("partuuid") ? row["partuuid"].asString() : new_uuid();
                require(uuid(id) && ids.insert(id).second,"invalid-layout-guid","New partition GUID is zero or duplicated"); }
            row["partuuid"]=id; result["partuuid"]=id; result["type_guid"]=old.isNull() ? Value(r.type) : old["type_guid"];
            result["start_lba"]=Json::UInt64(cursor/sector); result["end_lba"]=Json::UInt64((cursor+bytes)/sector-1); result["attributes"]=Json::UInt64(0);
            if(!old.isNull()) {
                require(result["end_lba"].asUInt64()<=old["end_lba"].asUInt64(),"layout-outside-userdata","Userdata cannot grow outside its original range");
                if(policy=="recreate")result["action"]="ERASE_AND_RECREATE_REQUIRED";
                else if(old["bytes"].asUInt64()>bytes)result["action"]="VERIFIED_SHRINK_REQUIRED";
                require(policy=="recreate" || result["start_lba"]==old["start_lba"],"userdata-migration-required","Preserved userdata must keep its original start");
                result["identity_changed"]=row["partuuid"]!=old["partuuid"]; result["destroys_existing_data"]=policy=="recreate";
            }
            result["payload_work_required"]=result["action"]!="UNCHANGED_GEOMETRY";
            result["filesystem_operation"]=old.isNull() ? "FORMAT_NEW_PARTITION_REQUIRED" : policy=="recreate" ? "ERASE_AND_FORMAT_USERDATA_REQUIRED" : "DETECT_FILESYSTEM_AND_VERIFY_SHRINK_SUPPORT";
            require(cursor>=start && bytes<=raw_end-cursor,"layout-outside-userdata","New partitions must fit wholly inside the original userdata extent"); cursor+=bytes;
        } else { require(!row.isMember("partuuid"),"invalid-layout-guid","Disabled roles do not accept a GUID"); result["payload_work_required"]=!old.isNull(); }
        request["rows"].append(row); rows.append(result);
    }
    const auto count=table["primary"]["entry_count"].asUInt();
    for(auto& row:rows)if(row["enabled"]==true) {
        unsigned slot=row["previous"].isNull() ? 0 : row["previous"]["index"].asUInt();
        if(slot==0) { for(unsigned candidate=1;candidate<=count;++candidate)if(!occupied.contains(candidate)) { slot=candidate; break; }
            require(slot>0 && occupied.insert(slot).second,"layout-table-full","No unused GPT entry is available"); }
        row["index"]=slot;
    }
    Value edits(Json::arrayValue); std::set<unsigned> edited;
    if(input.isMember("record_edits")) {
        require(input["record_edits"].isArray() && input["record_edits"].size()<=count,"invalid-record-edit","Record edits must be a bounded array");
        require(input["record_edits"].empty() || advanced,"advanced-mode-required","Editing another existing record requires advanced mode");
        for(auto change:input["record_edits"]) {
            keys(change,{"index","partuuid","contents","filesystem"});
            require(change["index"].isUInt() && edited.insert(change["index"].asUInt()).second,"invalid-record-edit","Select each existing GPT index once");
            Value previous; for(const auto& part:table["partitions"])if(part["index"].asUInt()==change["index"].asUInt())previous=part;
            require(!previous.isNull() && !userdata(previous),"protected-record-edit","Userdata uses its own row; OEM reservation records cannot be converted or formatted");
            change["index"]=previous["index"]; if(change.isMember("partuuid"))change["partuuid"]=normalized_uuid(change["partuuid"]);
            const auto contents=change.get("contents","preserve"); require(contents=="preserve" || contents=="format","invalid-record-edit","Select preserve or format contents");
            if(change.isMember("partuuid") && change["partuuid"]!=previous["partuuid"])
                require(change["partuuid"].isString() && uuid(change["partuuid"].asString()) && ids.insert(change["partuuid"].asString()).second,"invalid-layout-guid","Edited GUID is zero or duplicated");
            if(contents=="format")require(change["filesystem"]=="ext4" || change["filesystem"]=="f2fs" || change["filesystem"]=="fat32" || change["filesystem"]=="ntfs","invalid-layout-filesystem","Select an explicit format for the advanced content operation");
            else require(!change.isMember("filesystem"),"invalid-record-edit","Preserved contents do not accept a replacement filesystem");
            require(change.isMember("partuuid") || contents=="format","empty-record-edit","The selected record has no requested change");
            Value edit; edit["request"]=change; edit["previous"]=previous; edit["changes_geometry"]=false; edit["payload_work_required"]=contents=="format"; edit["destroys_existing_data"]=contents=="format"; edits.append(edit);
        }
    }
    request["record_edits"]=Value(Json::arrayValue); for(const auto& edit:edits)request["record_edits"].append(edit["request"]);
    Value out; out["schema"]=1; out["format"]="ure-partition-layout"; out["firmware_profile"]=profile; out["target_identity"]=target.identity;
    out["current_table_sha256"]=sha256(json(table)); out["request"]=request; out["rows"]=rows; out["protected_records"]=protected_parts;
    out["advanced_record_edits"]=edits; out["mode"]=mode; out["placement"]=placement; out["userdata_policy"]=policy;
    out["pool"]["source"]="ORIGINAL_USERDATA_ONLY"; out["pool"]["offset"]=Json::UInt64(start); out["pool"]["bytes"]=Json::UInt64(graph_pool); out["pool"]["end_offset"]=Json::UInt64(end);
    out["pool"]["original_bytes"]=original["bytes"]; out["pool"]["original_start_lba"]=original["start_lba"]; out["pool"]["original_end_lba"]=original["end_lba"];
    out["pool"]["start_padding_bytes"]=Json::UInt64(0); out["pool"]["end_padding_bytes"]=Json::UInt64(raw_end-end);
    out["allocated_bytes"]=Json::UInt64(cursor-start); out["unallocated_bytes"]=Json::UInt64(end-cursor); out["alignment_bytes"]=Json::UInt64(mib);
    out["percent_basis"]="ALIGNED_ORIGINAL_USERDATA_CAPACITY"; out["percent_basis_bytes"]=Json::UInt64(pool); out["read_only"]=true; out["physical_test_record"]=false; out["private_record"]=true;
    out["formats_filesystems"]=false; out["migrates_data"]=false; out["live_write_backend_ready"]=false; out["complete_partition_job"]=false;
    out["execution_scope"]="READ_ONLY_GPT_LAYOUT_PREVIEW"; out["live_repartition_blockers"]=partition_live_blockers();
    out["required_live_checks"]=Value(Json::arrayValue);
    for(const auto* check:{"DEVICE_AND_FIRMWARE_IDENTITY","VERIFIED_OFF_DEVICE_DATA_AND_GPT_BACKUPS","UFS_LUN_OWNERSHIP_AND_EXCLUSIVE_ACCESS","INSTALLED_ANDROID_FBE_TRUST","VIRTUAL_AB_MERGE_AND_SUPER_STATE","FILESYSTEM_SIZE_AND_SUPPORTED_SHRINK_OR_RECREATE","NEW_FILESYSTEM_FORMAT_AND_READBACK","STOCK_RECOVERY_ROUTE"})out["required_live_checks"].append(check);
    out["warnings"]=Value(Json::arrayValue);
    out["warnings"].append("GB uses 1,000,000,000 bytes; GiB uses 1,073,741,824 bytes; MiB uses 1,048,576 bytes. Allocations round down to whole MiB.");
    out["warnings"].append("Percentages use only the aligned original userdata extent. Free GPT gaps and all other partition ranges are excluded.");
    out["warnings"].append("Standard mode preserves userdata start, slot, type and GUID. New ESP/Linux/Windows partitions use only the tail released by a verified shrink.");
    if(policy=="recreate")out["warnings"].append("DATA LOSS: erase/recreate destroys Android userdata. Placement before userdata changes its start; it does not solve encryption or preserve existing encrypted data.");
    if(!edits.empty())out["warnings"].append("ADVANCED: explicitly selected existing GUID/content changes can break boot, Android, firmware or recovery. Unselected records and every non-userdata partition range remain unchanged.");
    out["warnings"].append("GPT metadata execution on an image does not resize, move or format its filesystem. Android FBE and Virtual A/B merge state are not authorized by this preview.");
    out["warnings"].append("Advanced mode changes reviewed choices only; it cannot bypass unaccepted device firmware, encryption trust, storage ownership or forced-restart durability.");
    out["warnings"].append("Advanced mode changes the requested plan, not device trust requirements. No live write is authorized by a mode switch.");
    out["warnings"].append("Xiaomi Pad 7 and POCO Pad X1 require their own detected model, firmware, capacity and original-unit evidence before any live operation.");
    out["layout_sha256"]=seal(out,"layout_sha256"); storage_revalidate(target,system); return out;
}
Value partition_layout_bar(const Value& layout,unsigned width) {
    require(width>0 && width<=32768 && layout["format"]=="ure-partition-layout" && layout["pool"]["bytes"].isUInt64() && layout["rows"].isArray() && layout["rows"].size()==4,
        "invalid-layout-bar","Invalid bounded layout graph"); const auto pool=layout["pool"]["bytes"].asUInt64(); require(pool>0 && pool<=INT64_MAX,"invalid-layout-bar","Invalid graph capacity");
    Value out(Json::arrayValue); std::uint64_t cursor=0;
    for(const auto& row:layout["rows"]) {
        require(row["role"].isString() && row["pool_offset"].isUInt64() && row["bytes"].isUInt64() && row["pool_offset"].asUInt64()==cursor && row["bytes"].asUInt64()<=pool-cursor,"invalid-layout-bar","Graph rows overlap or exceed capacity");
        (void)role(row["role"].asString()); const auto end=cursor+row["bytes"].asUInt64(); Value part; part["role"]=row["role"];
        part["x"]=Json::UInt(scaled(cursor,width,pool)); part["width"]=Json::UInt(scaled(end,width,pool)-part["x"].asUInt()); out.append(part); cursor=end;
    }
    Value remaining; remaining["role"]="unallocated"; remaining["x"]=Json::UInt(scaled(cursor,width,pool)); remaining["width"]=width-remaining["x"].asUInt(); out.append(remaining); return out;
}
std::string partition_layout_text(const Value& layout) {
    std::ostringstream out; out<<"Original userdata allocation pool: "<<display_size(layout["pool"]["bytes"].asUInt64())<<"\n";
    out<<"Mode: "<<layout["mode"].asString()<<" / "<<layout["placement"].asString()<<" / userdata: "<<layout["userdata_policy"].asString()<<"\n";
    out<<"Other existing records: "<<layout["protected_records"].size()<<"; explicit advanced edits: "<<layout["advanced_record_edits"].size()<<"\n\n";
    for(const auto& row:layout["rows"]) {
        out<<row["role"].asString()<<" / "<<row["filesystem"].asString()<<"\n";
        out<<"Before: "<<(row["previous"].isNull() ? "absent" : display_size(row["previous"]["bytes"].asUInt64()))<<"\nAfter: "<<display_size(row["bytes"].asUInt64())<<" / "<<row["action"].asString()<<"\n";
        out<<"Requested: "<<row["requested_size"].asString()<<" "<<row["unit"].asString()<<"; rounding: "<<row["rounding_bytes"].asUInt64()<<" bytes\n\n";
    }
    for(const auto& edit:layout["advanced_record_edits"]) {
        out<<"ADVANCED record "<<edit["previous"]["index"].asUInt()<<" / "<<edit["previous"]["label"].asString()<<"\n";
        out<<"GUID: "<<edit["previous"]["partuuid"].asString()<<" -> "<<edit["request"].get("partuuid",edit["previous"]["partuuid"]).asString()<<"\n";
        out<<"Contents: "<<edit["request"].get("contents","preserve").asString()<<"; formatting here: unavailable\n\n";
    }
    out<<"Unallocated: "<<display_size(layout["unallocated_bytes"].asUInt64())<<"\n\n";
    for(const auto& warning:layout["warnings"])out<<warning.asString()<<"\n\n";
    return out.str();
}
std::vector<StorageRange> gpt_layout_regions(const StorageTarget& target,const Value& request,const std::string& profile,Value& source,const Root* system) {
    source=partition_layout(target,request,profile,system); source["manifest_sha256"]=source["layout_sha256"];
    auto ranges=gpt_regions(target.descriptor.get(),target.identity["logical_sector_bytes"].asUInt());
    const auto table=gpt_inspect(target.descriptor.get(),target.identity["logical_sector_bytes"].asUInt());
    const auto count=table["primary"]["entry_count"].asUInt(),entry_size=table["primary"]["entry_size"].asUInt();
    const auto table_size=static_cast<std::size_t>(count)*entry_size; std::string entries;
    for(const auto& range:ranges)if(range.name=="primary_table")entries=range.bytes;
    require(entries.size()>=table_size,"invalid-layout","GPT table buffer is truncated");
    for(const auto& row:source["rows"])if(row["enabled"]==true) {
        const auto offset=static_cast<std::size_t>(row["index"].asUInt()-1)*entry_size;
        require(offset<=table_size && entry_size<=table_size-offset,"invalid-layout","Proposed GPT slot is outside the table");
        if(row["previous"].isNull()) {
            require(std::all_of(entries.begin()+static_cast<std::ptrdiff_t>(offset),entries.begin()+static_cast<std::ptrdiff_t>(offset+entry_size),[](char byte) { return byte==0; }),"protected-record-edit","New partitions require a completely unused GPT slot");
            entries.replace(offset,16,guid_bytes(row["type_guid"].asString()));
            const auto name=row["label"].asString(); for(std::size_t i=0;i<name.size();++i)put(entries,offset+56+i*2,static_cast<unsigned char>(name[i]),2);
        }
        entries.replace(offset+16,16,guid_bytes(row["partuuid"].asString()));
        put(entries,offset+32,row["start_lba"].asUInt64(),8); put(entries,offset+40,row["end_lba"].asUInt64(),8);
    }
    for(const auto& edit:source["advanced_record_edits"])if(edit["request"].isMember("partuuid"))
        entries.replace(static_cast<std::size_t>(edit["previous"]["index"].asUInt()-1)*entry_size+16,16,guid_bytes(edit["request"]["partuuid"].asString()));
    const auto sum=crc(std::string_view(entries).substr(0,table_size));
    for(auto& range:ranges) {
        if(range.name=="primary_table" || range.name=="backup_table") {
            require(range.bytes.size()==entries.size(),"invalid-layout","GPT table geometry differs"); range.bytes.replace(0,table_size,entries,0,table_size);
        } else if(range.name=="primary_header" || range.name=="backup_header") {
            put(range.bytes,88,sum,4); put(range.bytes,16,0,4); put(range.bytes,16,crc(std::string_view(range.bytes).substr(0,92)),4);
        }
    }
    return ranges;
}
} // namespace ure
