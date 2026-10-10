// SPDX-License-Identifier: Apache-2.0
// Every allocation comes from a verified original userdata extent. Labels never
// authorize reclaiming a factory partition or a free GPT gap.
#include "uke.h"
#include <algorithm>
#include <array>
#include <map>
#include <set>

namespace ure {
namespace {
constexpr std::uint64_t mib=1048576,gib=1073741824;
constexpr const char* linux_type="0fc63daf-8483-4772-8e79-3d69d8477de4";
const std::array<std::string,8> role_order{"userdata","esp","linux_boot","linux","linux_swap","windows","shared","linux2"};
const std::array<std::string,9> fs_order{"fat32","ntfs","f2fs","ext4","btrfs","zfs","exfat","linux-swap","xfs"};
bool role_known(const std::string& role) { return std::find(role_order.begin(),role_order.end(),role)!=role_order.end(); }
std::string hint(const std::string& label) {
    if(role_known(label))return label;
    if(label.starts_with("uke_") && role_known(label.substr(4)))return label.substr(4);
    return "unclassified";
}
std::string seal(Value value,const char* key) { value.removeMember(key); return sha256(json(value)); }
void fields(const Value& value,const std::set<std::string>& allowed) {
    require(value.isObject(),"invalid-multiboot-request","Expected multiboot settings");
    for(const auto& key:value.getMemberNames())require(allowed.contains(key),"invalid-multiboot-request","Unknown multiboot field: "+key);
}
std::uint64_t minimum(const std::string& role) {
    if(role=="esp" || role=="linux_boot")return 512000000;
    if(role=="linux" || role=="linux2")return 40*gib;
    if(role=="windows")return 50*gib;
    if(role=="linux_swap")return gib;
    if(role=="userdata")return 64*gib;
    return 32*mib;
}
std::string fixed_fs(const std::string& role) {
    return role=="esp" ? "fat32" : role=="windows" ? "ntfs" : role=="linux_swap" ? "linux-swap" : "";
}
const char* color(const std::string& filesystem) {
    if(filesystem=="fat32")return "#E5B849";
    if(filesystem=="ntfs")return "#4F87CE";
    if(filesystem=="f2fs")return "#ED8B3A";
    if(filesystem=="ext4")return "#68A65B";
    if(filesystem=="btrfs")return "#9D75C5";
    if(filesystem=="zfs")return "#487F83";
    if(filesystem=="exfat")return "#C78C65";
    if(filesystem=="linux-swap")return "#B56887";
    if(filesystem=="xfs")return "#5CA7A3";
    return "#939393";
}
std::string uuid_for(const std::string& seed) {
    auto id=sha256(seed).substr(0,32); id[12]='4'; id[16]="89ab"[static_cast<unsigned>(id[16])%4];
    return id.substr(0,8)+"-"+id.substr(8,4)+"-"+id.substr(12,4)+"-"+id.substr(16,4)+"-"+id.substr(20);
}
Value validate_request(const Value& input,std::uint64_t pool) {
    fields(input,{"schema","format","action","advanced","keep_userdata","userdata_filesystem","partitions","order"});
    require(input["schema"]==1 && input["format"]=="uke-multiboot-request" &&
        (input["action"]=="setup" || input["action"]=="restore-default") && input["advanced"].isBool() &&
        input["keep_userdata"].isBool() && (input["userdata_filesystem"]=="f2fs" || input["userdata_filesystem"]=="ext4"),
        "invalid-multiboot-request","Choose explicit userdata, filesystem and advanced settings");
    require(input["partitions"].isArray() && input["partitions"].size()<=7,"invalid-multiboot-request","Select at most seven multiboot partitions");
    const bool advanced=input["advanced"].asBool(),restore=input["action"]=="restore-default";
    require(!restore || (input["keep_userdata"]==true && input["partitions"].empty() && !input.isMember("order")),
        "invalid-multiboot-restore","Default restoration creates userdata only");
    require(restore || !input["partitions"].empty(),"no-multiboot-partition","Select at least one multiboot partition");
    require(!input.isMember("order") || advanced,"advanced-mode-required","Custom order requires advanced mode");
    std::map<std::string,Value> selected; std::set<std::string> labels; std::uint64_t allocated=0;
    for(auto row:input["partitions"]) {
        fields(row,{"role","size","unit","filesystem","label"});
        require(row["role"].isString() && role_known(row["role"].asString()) && row["role"]!="userdata" &&
            row["size"].isString() && row["unit"].isString() && row["filesystem"].isString(),"invalid-multiboot-selection","Select a supported partition role, size and filesystem");
        const auto role=row["role"].asString(),fs=row["filesystem"].asString();
        require(std::find(fs_order.begin(),fs_order.end(),fs)!=fs_order.end(),"invalid-layout-filesystem","Select a supported filesystem name");
        require(fixed_fs(role).empty() || fs==fixed_fs(role),"fixed-multiboot-filesystem","ESP, Windows and Linux swap filesystem types are fixed");
        require(!row.isMember("label") || advanced,"advanced-mode-required","Custom GPT names require advanced mode");
        const auto label=row.get("label",role).asString();
        require(identifier(label) && label.size()<=36 && label!="userdata" && labels.insert(label).second,
            "invalid-multiboot-label","GPT names must be unique ASCII identifiers of at most 36 characters");
        require(!role_known(label) || label==role,"invalid-multiboot-label","A custom name cannot impersonate another multiboot role");
        const auto amount=layout_size_bytes(row["size"].asString(),row["unit"].asString(),pool);
        require(amount>=minimum(role),"multiboot-partition-too-small","The requested allocation is below the selected partition minimum");
        require(amount<=pool,"insufficient-layout-space","Selected allocation exceeds original userdata capacity");
        const auto bytes=(amount/mib+(amount%mib ? 1 : 0))*mib;
        require(role!="esp" || bytes<=4*gib,"esp-too-large","ESP cannot exceed 4 GiB");
        require(bytes<=pool-allocated,"insufficient-layout-space","Selected allocations exceed original userdata capacity"); allocated+=bytes;
        row["label"]=label; row["requested_bytes"]=Json::UInt64(amount); row["bytes"]=Json::UInt64(bytes);
        require(selected.emplace(role,row).second,"duplicate-multiboot-role","Select every multiboot role only once");
    }
    require(!selected.contains("windows") || selected.contains("esp"),"windows-requires-esp","Windows requires an ESP");
    require(!selected.contains("linux_boot") || selected.contains("linux") || selected.contains("linux2"),"linux-boot-without-linux","A separate Linux boot partition requires a Linux root");
    if(input["keep_userdata"]==true) {
        const auto bytes=pool-allocated;
        require(restore || bytes>=minimum("userdata"),"userdata-too-small","Keep at least 64 GiB for Android userdata or explicitly omit userdata");
        Value row; row["role"]="userdata"; row["label"]="userdata"; row["filesystem"]=input["userdata_filesystem"];
        row["size"]=""; row["unit"]="remaining"; row["bytes"]=Json::UInt64(bytes); row["requested_bytes"]=Json::UInt64(bytes); selected.emplace("userdata",row);
    }
    Value out; out["rows"]=Value(Json::arrayValue); out["request"]=input; out["request"]["partitions"]=Value(Json::arrayValue);
    std::vector<std::string> order;
    if(input.isMember("order")) {
        require(input["order"].isArray() && input["order"].size()==selected.size(),"invalid-multiboot-order","Order must include every selected partition once");
        std::set<std::string> seen;
        for(const auto& role:input["order"]) { require(role.isString() && selected.contains(role.asString()) && seen.insert(role.asString()).second,
            "invalid-multiboot-order","Custom order contains a missing, duplicate or disabled role"); order.push_back(role.asString()); }
    } else for(const auto& role:role_order)if(selected.contains(role))order.push_back(role);
    for(const auto& role:order)out["rows"].append(selected.at(role));
    // Normalize selection order independently of the display order so plans are reproducible.
    for(const auto& role:role_order)if(role!="userdata" && selected.contains(role)) {
        auto row=selected.at(role); row.removeMember("bytes"); row.removeMember("requested_bytes");
        if(!advanced)row.removeMember("label");
        out["request"]["partitions"].append(row);
    }
    return out;
}
void put(std::string& data,std::size_t offset,std::uint64_t value,unsigned bytes) {
    require(offset<=data.size() && bytes<=data.size()-offset,"invalid-multiboot-gpt","GPT encoding exceeds its buffer");
    for(unsigned i=0;i<bytes;++i)data[offset+i]=static_cast<char>((value>>(i*8))&255);
}
std::uint32_t crc(std::string_view bytes) {
    std::uint32_t out=UINT32_MAX;
    for(char byte:bytes) { out^=static_cast<unsigned char>(byte); for(unsigned i=0;i<8;++i)out=(out>>1)^((out&1U) ? 0xedb88320U : 0U); } return out^UINT32_MAX;
}
std::string guid(const std::string& id) {
    require(uuid(id),"invalid-multiboot-gpt","GPT GUID is invalid"); std::string hex;
    for(char byte:id)if(byte!='-')hex+=byte;
    auto nibble=[](char byte) { return static_cast<unsigned>(byte<='9' ? byte-'0' : (byte>='a' ? byte-'a' : byte-'A')+10); };
    constexpr std::array<unsigned,16> order{3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15}; std::string out(16,'\0');
    for(unsigned i=0;i<16;++i)out[order[i]]=static_cast<char>((nibble(hex[i*2])<<4)|nibble(hex[i*2+1]));
    return out;
}
}

Value multiboot_capabilities() {
    const auto available=filesystem_capabilities(); Value out; out["schema"]=1; out["filesystems"]=Value(Json::arrayValue); out["roles"]=Value(Json::arrayValue);
    for(const auto& fs:fs_order) {
        Value row; row["filesystem"]=fs; row["color"]=color(fs); row["format_available"]=false; row["check_available"]=false;
        const auto type=fs=="fat32" ? "vfat" : fs;
        for(const auto& capability:available["filesystems"])if(capability["filesystem"]==type) {
            row["format_available"]=capability["format_available"]; row["check_available"]=capability["check_available"];
        }
        row["creation_available"]=row["format_available"]==true && row["check_available"]==true;
        row["blocker"]=row["creation_available"]==true ? "" : "reviewed-formatter-and-independent-checker-required"; out["filesystems"].append(row);
    }
    for(const auto& role:role_order) {
        Value row; row["role"]=role; row["minimum_bytes"]=Json::UInt64(minimum(role)); row["fixed_filesystem"]=fixed_fs(role);
        if(role=="esp")row["maximum_bytes"]=Json::UInt64(4*gib);
        out["roles"].append(row);
    }
    out["allocation_pool"]="ORIGINAL_USERDATA_ONLY"; out["live_write_backend_ready"]=false; out["physical_test_record"]=false; return out;
}
Value multiboot_inspect(const StorageTarget& target,const fs::path& original_backup,const std::string& profile,const Root* system) {
    require(identifier(profile),"invalid-profile","Select an explicit firmware profile"); storage_revalidate(target,system);
    require(target.identity["kind"]=="regular-image" || target.identity["partition"]==false,"invalid-target","Multiboot selects the userdata disk/LUN");
    const auto sector=target.identity["logical_sector_bytes"].asUInt(); const auto current=gpt_inspect(target.descriptor.get(),sector);
    require(current["healthy"]==true,"unhealthy-layout-source","Two agreeing valid GPT copies are required");
    require(current["partitions"].size()<=128 && current["reserved_records"].size()<=128,"partition-probe-limit","Multiboot inspection is bounded to 128 GPT records");
    const auto metadata=gpt_regions(target.descriptor.get(),sector); Value original=current,proof;
    if(!original_backup.empty()) { proof=gpt_original_table(target,original_backup,profile,system); original=proof["table"]; }
    else for(const auto& part:current["partitions"])require(hint(part["label"].asString())=="unclassified" || part["label"]=="userdata",
        "original-gpt-required","An existing multiboot layout requires a verified original GPT backup");
    require(original["healthy"]==true && original["disk_guid"]==current["disk_guid"],"invalid-backup","Original GPT identity is unavailable or differs");
    for(const auto* key:{"first_usable_lba","last_usable_lba","entry_size","entries_lba"})for(const auto* side:{"primary","backup"})
        require(json(original[side][key])==json(current[side][key]),"gpt-geometry-changed","GPT locations, usable bounds and record sizes must match original evidence");
    Value userdata,protected_parts(Json::arrayValue);
    for(const auto& part:original["partitions"]) {
        if(part["label"]=="userdata") { require(userdata.isNull(),"ambiguous-user-area","Original GPT must have exactly one userdata partition"); userdata=part; }
        else { require(hint(part["label"].asString())=="unclassified","original-gpt-not-stock","Original evidence already contains multiboot roles"); protected_parts.append(part); }
    }
    for(const auto& part:original["reserved_records"])protected_parts.append(part);
    require(!userdata.isNull() && userdata["attributes"].isUInt64() && userdata["attributes"].asUInt64()==0,"user-area-unavailable","Unattributed original userdata evidence is required");
    const auto begin=userdata["start_lba"].asUInt64()*sector,end=(userdata["end_lba"].asUInt64()+1)*sector;
    require(begin%mib==0 && end>begin && end-begin<=512*gib,"invalid-user-area","Original userdata must be aligned and at most 512 GiB");
    std::map<unsigned,Value> protected_by_index;
    for(const auto& part:protected_parts)protected_by_index.emplace(part["index"].asUInt(),part);
    Value old_rows(Json::arrayValue); bool changed=false; std::set<unsigned> found;
    auto inspect=[&](const Value& part,bool reserved) {
        const auto index=part["index"].asUInt(); const auto it=protected_by_index.find(index);
        if(it!=protected_by_index.end()) { require(json(it->second)==json(part),"protected-partition-changed","A factory GPT record differs from original evidence"); found.insert(index); return; }
        const auto offset=part["start_lba"].asUInt64()*sector,finish=(part["end_lba"].asUInt64()+1)*sector;
        require(!reserved && offset>=begin && finish<=end,"partition-outside-original-userdata","An unknown GPT record escapes the proven original userdata region");
        auto row=part; row["offset"]=Json::UInt64(offset); row["bytes"]=Json::UInt64(finish-offset); row["pool_offset"]=Json::UInt64(offset-begin); row["role"]=hint(part["label"].asString());
        row["filesystem"]=filesystem_probe_range(target.descriptor.get(),offset,finish-offset)["type"];
        if(row["filesystem"]=="vfat")row["filesystem"]="fat32";
        row["color"]=color(row["filesystem"].asString()); old_rows.append(row);
        changed=changed || json(part)!=json(userdata);
    };
    for(const auto& part:current["partitions"])inspect(part,false);
    for(const auto& part:current["reserved_records"])inspect(part,true);
    require(found.size()==protected_by_index.size(),"protected-partition-changed","A factory GPT record is missing");
    require(!original_backup.empty() || !changed,"original-gpt-required","Changed userdata requires original GPT evidence");
    std::vector<Value> ordered(old_rows.begin(),old_rows.end());
    std::sort(ordered.begin(),ordered.end(),[](const Value& a,const Value& b){return a["offset"].asUInt64()<b["offset"].asUInt64();});
    old_rows=Value(Json::arrayValue); for(const auto& row:ordered)old_rows.append(row);
    Value out; out["schema"]=1; out["format"]="ure-multiboot-inspection"; out["existing_multiboot"]=changed; out["multiboot"]=changed; out["old_rows"]=old_rows;
    out["original_userdata"]=userdata; out["original_table"]=original; out["current_table"]=current; out["protected_records"]=protected_parts;
    out["original_backup_directory"]=original_backup.empty() ? "" : fs::absolute(original_backup).lexically_normal().string();
    out["original_manifest_sha256"]=proof.isNull() ? sha256(json(current)) : proof["manifest"]["manifest_sha256"];
    out["original_basis"]=proof.isNull() ? "CURRENT_UNMODIFIED_USERDATA_GPT" : "VERIFIED_SAME_TARGET_ORIGINAL_GPT_BACKUP";
    out["pool"]["source"]="ORIGINAL_USERDATA_ONLY"; out["pool"]["offset"]=Json::UInt64(begin);
    out["pool"]["original_bytes"]=Json::UInt64(end-begin); out["pool"]["bytes"]=Json::UInt64((end-begin)/mib*mib);
    out["pool"]["end_offset"]=Json::UInt64(end); out["read_only"]=true; out["physical_test_record"]=false;
    const auto now=gpt_regions(target.descriptor.get(),sector); require(now.size()==metadata.size(),"stale-device","GPT changed during inspection");
    for(std::size_t i=0;i<now.size();++i)require(now[i].offset==metadata[i].offset && now[i].bytes==metadata[i].bytes,"stale-device","GPT changed during inspection");
    storage_revalidate(target,system); return out;
}
std::vector<StorageRange> multiboot_gpt_regions(const StorageTarget& target,const Value& input,const fs::path& original_backup,
    const std::string& profile,Value& source,const Root* system) {
    const auto inspection=multiboot_inspect(target,original_backup,profile,system);
    require(input["action"]!="restore-default" || !original_backup.empty(),"original-gpt-required","Restore default requires a verified original GPT backup");
    const bool restore=input["action"]=="restore-default";
    const auto budget=inspection["pool"][restore ? "original_bytes" : "bytes"].asUInt64();
    const auto normalized=validate_request(input,budget);
    auto ranges=gpt_regions(target.descriptor.get(),target.identity["logical_sector_bytes"].asUInt());
    const auto sector=target.identity["logical_sector_bytes"].asUInt(),entry_size=inspection["current_table"]["primary"]["entry_size"].asUInt();
    require(entry_size==128,"unsupported-gpt-entry-size","Multiboot requires standard 128-byte GPT records");
    auto count=inspection[restore ? "original_table" : "current_table"]["primary"]["entry_count"].asUInt(); std::string entries;
    for(const auto& range:ranges)if(range.name=="primary_table")entries=range.bytes;
    std::set<unsigned> protected_indices; std::set<std::string> ids,labels;
    for(const auto& part:inspection["protected_records"]) { protected_indices.insert(part["index"].asUInt()); ids.insert(part["partuuid"].asString()); labels.insert(part["label"].asString()); }
    unsigned needed=static_cast<unsigned>(normalized["rows"].size());
    while(count-protected_indices.size()<needed)count+=32;
    require(count<=128 && static_cast<std::size_t>(count)*entry_size<=entries.size(),"layout-table-full","The existing GPT reservation cannot hold selected partitions");
    const auto old_count=inspection["current_table"]["primary"]["entry_count"].asUInt();
    if(count>old_count)for(const auto& range:ranges)if(range.name=="primary_table" || range.name=="backup_table") {
        const auto start=static_cast<std::size_t>(old_count)*entry_size,finish=static_cast<std::size_t>(count)*entry_size;
        require(finish<=range.bytes.size() && std::all_of(range.bytes.begin()+static_cast<std::ptrdiff_t>(start),range.bytes.begin()+static_cast<std::ptrdiff_t>(finish),[](char byte){return byte==0;}),
            "layout-table-reserve-not-empty","GPT expansion requires independently verified zero reservation bytes");
    }
    for(unsigned index=1;index<=std::max(count,old_count);++index)if(!protected_indices.contains(index))entries.replace(static_cast<std::size_t>(index-1)*entry_size,entry_size,entry_size,'\0');
    Value rows(Json::arrayValue); std::uint64_t cursor=inspection["pool"]["offset"].asUInt64();
    std::set<unsigned> occupied=protected_indices;
    for(auto row:normalized["rows"]) {
        const auto role=row["role"].asString(); auto bytes=row["bytes"].asUInt64();
        if(input["action"]=="restore-default")bytes=inspection["pool"]["original_bytes"].asUInt64();
        auto index=role=="userdata" ? inspection["original_userdata"]["index"].asUInt() : 0;
        if(index==0) { for(unsigned candidate=1;candidate<=count;++candidate)if(!occupied.contains(candidate) &&
            !(input["keep_userdata"]==true && candidate==inspection["original_userdata"]["index"].asUInt())) { index=candidate; break; } }
        require(index>0 && occupied.insert(index).second,"layout-table-full","No unused GPT record is available");
        require(labels.insert(row["label"].asString()).second,"invalid-multiboot-label","A selected GPT name duplicates a protected factory name");
        const auto id=role=="userdata" ? inspection["original_userdata"]["partuuid"].asString() : uuid_for(inspection["original_manifest_sha256"].asString()+json(normalized["request"])+role);
        require(ids.insert(id).second,"invalid-layout-guid","Partition GUID collides with another record");
        row["index"]=index; row["partuuid"]=id; row["type_guid"]=role=="userdata" ? inspection["original_userdata"]["type_guid"] :
            Value(role=="esp" ? "c12a7328-f81f-11d2-ba4b-00a0c93ec93b" : role=="windows" || role=="shared" ? "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7" : role=="linux_swap" ? "0657fd6d-a4ab-43c4-84e5-0933c84b4f4f" : linux_type);
        row["enabled"]=true; row["bytes"]=Json::UInt64(bytes); row["offset"]=Json::UInt64(cursor); row["pool_offset"]=Json::UInt64(cursor-inspection["pool"]["offset"].asUInt64());
        row["start_lba"]=Json::UInt64(cursor/sector); row["end_lba"]=Json::UInt64((cursor+bytes)/sector-1); row["attributes"]=Json::UInt64(0);
        row["action"]="ERASE_AND_RECREATE_REQUIRED"; row["previous"]=Value(); row["color"]=color(row["filesystem"].asString());
        const auto offset=static_cast<std::size_t>(index-1)*entry_size; entries.replace(offset,16,guid(row["type_guid"].asString())); entries.replace(offset+16,16,guid(id));
        put(entries,offset+32,row["start_lba"].asUInt64(),8); put(entries,offset+40,row["end_lba"].asUInt64(),8);
        const auto label=row["label"].asString(); for(std::size_t i=0;i<label.size();++i)put(entries,offset+56+i*2,static_cast<unsigned char>(label[i]),2);
        cursor+=bytes; rows.append(row);
    }
    const auto sum=crc(std::string_view(entries).substr(0,static_cast<std::size_t>(count)*entry_size));
    for(auto& range:ranges) {
        if(range.name=="primary_table" || range.name=="backup_table")range.bytes.replace(0,static_cast<std::size_t>(count)*entry_size,entries,0,static_cast<std::size_t>(count)*entry_size);
        else if(range.name=="primary_header" || range.name=="backup_header") { put(range.bytes,80,count,4); put(range.bytes,88,sum,4); put(range.bytes,16,0,4); put(range.bytes,16,crc(std::string_view(range.bytes).substr(0,92)),4); }
    }
    source=inspection; source["format"]="ure-multiboot-layout"; source["request"]=normalized["request"]; source["rows"]=rows;
    source["userdata_policy"]="recreate"; source["advanced_record_edits"]=Value(Json::arrayValue); source["warnings"]=Value(Json::arrayValue);
    source["warnings"].append("DATA LOSS: every selected filesystem replaces data within original userdata. Factory partition payloads and records remain unchanged.");
    source["warnings"].append("Android encryption keys are not recreated by formatting userdata. Physical Android boot and encryption acceptance remain pending.");
    if(input["keep_userdata"]==false)source["warnings"].append("Android userdata is omitted. Android cannot boot normally until userdata is restored.");
    source["pool"]["bytes"]=Json::UInt64(budget); source["allocated_bytes"]=Json::UInt64(cursor-inspection["pool"]["offset"].asUInt64());
    source["layout_sha256"]=seal(source,"layout_sha256"); source["manifest_sha256"]=source["layout_sha256"]; return ranges;
}
void multiboot_validate_plan(const Value& plan) {
    require(plan["schema"]==1 && plan["format"]=="ure-multiboot-plan" && plan["operation"]=="multiboot.setup" &&
        hash_valid(plan["plan_sha256"].asString()) && seal(plan,"plan_sha256")==plan["plan_sha256"].asString() &&
        plan["pool"]["source"]=="ORIGINAL_USERDATA_ONLY" && plan["writes_factory_partition_payloads"]==false && plan["data_loss"]==true &&
        plan["physical_test_record"]==false && plan["live_write_backend_ready"]==false,"invalid-multiboot-plan","Invalid or changed multiboot plan");
    const auto normalized=validate_request(plan["request"],plan["pool"]["bytes"].asUInt64());
    require(json(normalized["request"])==json(plan["request"]),"invalid-multiboot-plan","Multiboot selections are not canonical");
    require(plan["confirmation_phrase"]==(plan["request"]["action"]=="restore-default" ? "RESTORE DEFAULT" : "ERASE USERDATA"),
        "invalid-multiboot-plan","The reviewed action and data-loss consent differ");
    const auto& layout=plan["gpt"]["layout"];
    for(const auto* key:{"pool","old_rows","original_backup_directory","original_manifest_sha256"})
        require(json(layout[key])==json(plan[key]),"invalid-multiboot-plan","The displayed original region differs from the GPT job");
    require(json(layout["rows"])==json(plan["new_rows"]) && json(layout["request"])==json(plan["request"]) &&
        json(plan["gpt"]["target_identity"])==json(plan["target_identity"]),"invalid-multiboot-plan","The displayed selection differs from the GPT job");
    if(plan["executable"]==true)require(plan["image_job"]["gpt"]["layout"]["format"]=="ure-multiboot-layout" &&
        json(plan["image_job"]["gpt"])==json(plan["gpt"]),"invalid-multiboot-plan","The executable image job differs from selections");
}
Value multiboot_plan(const Root& system,const StorageTarget& target,const Value& input,const fs::path& original_backup,const std::string& profile) {
    const auto metadata=gpt_multiboot_plan(target,input,original_backup,profile,&system); const auto& layout=metadata["layout"];
    Value plan; plan["schema"]=1; plan["format"]="ure-multiboot-plan"; plan["operation"]="multiboot.setup";
    plan["request"]=layout["request"]; plan["target_identity"]=target.identity; plan["firmware_profile"]=profile; plan["pool"]=layout["pool"];
    plan["old_rows"]=layout["old_rows"]; plan["new_rows"]=layout["rows"]; plan["gpt"]=metadata;
    plan["original_backup_directory"]=layout["original_backup_directory"]; plan["original_manifest_sha256"]=layout["original_manifest_sha256"];
    plan["commands"]=Value(Json::arrayValue); plan["blockers"]=Value(Json::arrayValue); plan["executable"]=false;
    plan["read_only"]=true; plan["data_loss"]=true; plan["writes_factory_partition_payloads"]=false; plan["physical_test_record"]=false; plan["live_write_backend_ready"]=false;
    plan["confirmation_phrase"]=input["action"]=="restore-default" ? "RESTORE DEFAULT" : "ERASE USERDATA"; plan["warnings"]=layout["warnings"];
    const auto capabilities=multiboot_capabilities();
    for(const auto& row:layout["rows"]) {
        Value command; command["operation"]="erase-and-format"; command["role"]=row["role"]; command["label"]=row["label"];
        command["filesystem"]=row["filesystem"]; command["offset"]=row["offset"]; command["bytes"]=row["bytes"]; command["scope"]="original-userdata-only"; plan["commands"].append(command);
        for(const auto& fs:capabilities["filesystems"])if(fs["filesystem"]==row["filesystem"] && fs["creation_available"]!=true) {
            Value blocker; blocker["code"]="missing-filesystem-adapter"; blocker["filesystem"]=row["filesystem"]; blocker["role"]=row["role"]; plan["blockers"].append(blocker);
        }
    }
    Value gpt; gpt["operation"]="journal-and-write-reviewed-GPT"; gpt["factory_records_unchanged"]=true; plan["commands"].append(gpt);
    if(target.identity["kind"]!="regular-image")for(const auto& blocker:partition_live_blockers())plan["blockers"].append(blocker);
    if(target.identity["kind"]=="regular-image" && plan["blockers"].empty()) {
        plan["image_job"]=partition_job_multiboot_plan(system,target,plan["request"],original_backup,profile); plan["executable"]=true;
        // Reuse the job's exact generated GPT plan, including its operation identity.
        plan["gpt"]=plan["image_job"]["gpt"];
    }
    plan["plan_sha256"]=seal(plan,"plan_sha256"); multiboot_validate_plan(plan); return plan;
}
Value multiboot_image_execute(const Root& system,StorageTarget& target,const Value& plan,const fs::path& journal,
    const std::string& confirmation,const std::string& data_policy) {
    multiboot_validate_plan(plan);
    require(confirmation==plan["plan_sha256"].asString() && data_policy==plan["confirmation_phrase"].asString(),"confirmation-required","Confirm the exact multiboot plan and data-loss policy");
    require(target.identity["kind"]=="regular-image" && plan["executable"]==true,"live-repartition-unavailable","No accepted live multiboot writer is available");
    require(json(target.identity)==json(plan["target_identity"]),"stale-device","Multiboot target changed after review");
    const auto result=partition_job_execute_reviewed(system,target,plan["image_job"],journal,plan["image_job"]["plan_sha256"].asString(),plan);
    return result;
}
} // namespace ure
