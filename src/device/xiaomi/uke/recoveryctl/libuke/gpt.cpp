// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include "operation_guard.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <map>
#include <set>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace ure {
namespace {
constexpr std::size_t range_limit=4*1024*1024;
class TargetLock {
    int fd_;
public:
    explicit TargetLock(int fd) : fd_(fd) {
        require(::flock(fd_,LOCK_EX|LOCK_NB)==0,"busy-target","Another process owns this GPT target");
    }
    ~TargetLock() { ::flock(fd_,LOCK_UN); }
    TargetLock(const TargetLock&)=delete;
    TargetLock& operator=(const TargetLock&)=delete;
};
std::string seal(Value value,const std::string& key) { value.removeMember(key); return sha256(json(value)); }
bool tables_equal(Value a,Value b) {
    // Older schema-1 records predate the explicit OEM reservation inventory.
    // Only an empty additive inventory can be omitted without changing meaning.
    if(!a.isMember("reserved_records") && b["reserved_records"].isArray() && b["reserved_records"].empty())b.removeMember("reserved_records");
    if(!b.isMember("reserved_records") && a["reserved_records"].isArray() && a["reserved_records"].empty())a.removeMember("reserved_records");
    return json(a)==json(b);
}
int order(const std::string& name);
Value descriptions(const std::vector<StorageRange>& ranges) {
    Value result(Json::arrayValue);
    for(const auto& range:ranges) {
        Value item; item["name"]=range.name; item["offset"]=Json::UInt64(range.offset);
        item["bytes"]=Json::UInt64(range.bytes.size()); item["sha256"]=sha256(range.bytes); result.append(item);
    }
    return result;
}
void write_all(int fd,std::string_view data,std::uint64_t offset=0) {
    while(!data.empty()) {
        const auto n=::pwrite(fd,data.data(),data.size(),static_cast<off_t>(offset));
        if(n<0 && errno==EINTR)continue;
        require(n>0,"io-error","GPT write failed"); offset+=static_cast<std::uint64_t>(n); data.remove_prefix(static_cast<std::size_t>(n));
    }
    require(::fsync(fd)==0,"uncertain-write","Cannot sync GPT data");
}
void private_file(const Root& root,const std::string& name) {
    const auto st=root.stat(name);
    require(S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==::geteuid() && (st.st_mode & 07777)==0600,
        "unsafe-journal","GPT record must be an owned mode-0600 regular file");
}
Value record(const Root& root,const std::string& name) {
    private_file(root,name); return parse_json(root.read(name,range_limit));
}
void store_bytes(const Root& store,const std::string& name,const std::string& bytes) {
    auto out=store.open(name,O_RDWR|O_CREAT|O_EXCL,0600); write_all(out.get(),bytes);
    require(sha256(out.get())==sha256(bytes),"backup-corrupt","GPT backup readback differs");
    require(::fsync(store.fd())==0,"io-error","Cannot sync GPT backup directory");
}
void check_ranges(const Value& list,std::uint64_t capacity,std::uint32_t sector) {
    require(list.isArray() && !list.empty() && list.size()<=5,"invalid-gpt-plan","Invalid GPT range list");
    std::set<std::string> names; std::vector<std::pair<std::uint64_t,std::uint64_t>> used;
    for(const auto& range:list) {
        require(range["name"].isString() && identifier(range["name"].asString()) && names.insert(range["name"].asString()).second &&
            range["offset"].isUInt64() && range["bytes"].isUInt64() && range["bytes"].asUInt64()>0 &&
            range["bytes"].asUInt64()<=range_limit && range["sha256"].isString() && hash_valid(range["sha256"].asString()),
            "invalid-gpt-plan","Malformed or duplicated GPT range");
        const auto begin=range["offset"].asUInt64(),size=range["bytes"].asUInt64();
        require(begin%sector==0 && size%sector==0 && begin<=capacity && size<=capacity-begin,"invalid-gpt-plan","GPT range is unaligned or outside storage");
        const auto role=order(range["name"].asString());
        require((role==0 || role==2) ? size>=16384 && begin>sector :
            size==sector && begin==(role==1 ? capacity-sector : role==3 ? sector : 0),
            "invalid-gpt-plan","GPT role does not match its size or header position");
        used.emplace_back(begin,begin+size);
    }
    std::sort(used.begin(),used.end()); for(std::size_t i=1;i<used.size();++i)require(used[i-1].second<=used[i].first,"invalid-gpt-plan","GPT ranges overlap");
}
Value backup_manifest(const Root& store) {
    auto manifest=record(store,"manifest.json");
    require(manifest["schema"]==1 && manifest["format"]=="ure-gpt-backup" && manifest["target_identity"].isObject() &&
        manifest["target_identity"]["bytes"].isUInt64() && manifest["target_identity"]["logical_sector_bytes"].isUInt() &&
        manifest["firmware_profile"].isString() && identifier(manifest["firmware_profile"].asString()) &&
        manifest["manifest_sha256"].isString() && hash_valid(manifest["manifest_sha256"].asString()) &&
        manifest["manifest_sha256"].asString()==seal(manifest,"manifest_sha256"),"invalid-backup","GPT backup manifest checksum or schema differs");
    const auto sector=manifest["target_identity"]["logical_sector_bytes"].asUInt();
    require(sector==512 || sector==4096,"invalid-backup","Invalid GPT backup sector size");
    check_ranges(manifest["regions"],manifest["target_identity"]["bytes"].asUInt64(),sector);
    private_file(store,"partition-table.json");
    require(manifest["partition_table_sha256"].isString() && sha256(store.read("partition-table.json",range_limit))==manifest["partition_table_sha256"].asString(),
        "backup-corrupt","GPT partition table record differs from manifest");
    for(const auto& range:manifest["regions"]) {
        const auto name=range["name"].asString()+".bin"; private_file(store,name);
        auto fd=store.open(name,O_RDONLY|O_NONBLOCK); const auto st=store.stat(name);
        require(st.st_size>=0 && static_cast<std::uint64_t>(st.st_size)==range["bytes"].asUInt64() && sha256(fd.get())==range["sha256"].asString(),
            "backup-corrupt","GPT region size or hash differs from manifest");
    }
    require(manifest["regions"].size()==5,"invalid-backup","A complete GPT backup contains exactly five metadata ranges");
    Fd reconstruction(::memfd_create("ure-gpt-validation",MFD_CLOEXEC));
    require(reconstruction.get()>=0 && manifest["target_identity"]["bytes"].asUInt64()<=INT64_MAX &&
        ::ftruncate(reconstruction.get(),static_cast<off_t>(manifest["target_identity"]["bytes"].asUInt64()))==0,
        "validation-unavailable","Cannot reconstruct sparse GPT metadata for verification");
    std::string sums;
    for(const auto& range:manifest["regions"]) {
        const auto name=range["name"].asString()+".bin";
        write_all(reconstruction.get(),store.read(name,range_limit),range["offset"].asUInt64());
        sums+=range["sha256"].asString()+"  "+name+"\n";
    }
    const auto actual=gpt_inspect(reconstruction.get(),sector);
    require(actual["disk_guid"]==manifest["target_identity"]["disk_guid"] && tables_equal(actual,record(store,"partition-table.json")) &&
        json(descriptions(gpt_regions(reconstruction.get(),sector)))==json(manifest["regions"]),
        "invalid-backup","Backup bytes do not encode the recorded GPT or metadata geometry");
    sums+=manifest["partition_table_sha256"].asString()+"  partition-table.json\n"+sha256(store.read("manifest.json",range_limit))+"  manifest.json\n";
    private_file(store,"SHA256SUMS"); require(store.read("SHA256SUMS",8192)==sums,"backup-corrupt","GPT checksum list differs");
    return manifest;
}
std::vector<StorageRange> load_ranges(const Root& store,const Value& manifest) {
    std::vector<StorageRange> ranges;
    for(const auto& range:manifest["regions"])ranges.push_back({range["name"].asString(),range["offset"].asUInt64(),store.read(range["name"].asString()+".bin",range_limit)});
    return ranges;
}
void same_target(const Value& current,const Value& original) {
    require(current["kind"]==original["kind"],"wrong-target","GPT backup source kind differs");
    for(const auto* key:{"bytes","logical_sector_bytes","device_family"})require(json(current[key])==json(original[key]),"wrong-target","GPT capacity, sector size or device family differs");
    if(current["kind"]=="regular-image") {
        for(const auto* key:{"path","file_device","file_inode","uid","gid","mode"})require(json(current[key])==json(original[key]),"wrong-target","GPT backup belongs to another image or changed ownership/mode");
    } else {
        require(current["unit_identity_available"]==true && original["unit_identity_available"]==true &&
            current["unit_identity_sha256"]==original["unit_identity_sha256"] && current["lun_address"]==original["lun_address"] &&
            current["partition"]==false && original["partition"]==false,"wrong-target","GPT unit/LUN identity is missing or differs");
    }
    if(!current["disk_guid"].isNull())require(current["disk_guid"]==original["disk_guid"],"wrong-target","Current GPT disk GUID differs from backup");
}
std::vector<StorageRange> desired(const StorageTarget& target,const std::string& operation,const fs::path& backup,const std::string& profile,Value& source) {
    const auto sector=target.identity["logical_sector_bytes"].asUInt();
    if(operation=="gpt.repair")return gpt_repair_regions(target.descriptor.get(),sector);
    require(operation=="gpt.restore" && !backup.empty(),"invalid-gpt-plan","Restore requires an explicit verified GPT backup");
    auto store=private_directory(backup,false); source=backup_manifest(store); same_target(target.identity,source["target_identity"]);
    require(source["firmware_profile"]==profile,"wrong-profile","GPT backup profile differs from the selected profile");
    const auto table=parse_json(store.read("partition-table.json",range_limit));
    require(table["healthy"]==true,"invalid-backup","Restore requires a backup with two matching valid GPT copies");
    return load_ranges(store,source);
}
std::vector<StorageRange> read_original(int fd,const std::vector<StorageRange>& desired_ranges) {
    std::vector<StorageRange> before;
    for(const auto& range:desired_ranges)before.push_back({range.name,range.offset,storage_read(fd,range.offset,range.bytes.size())});
    return before;
}
Value untouched_regions(int fd,std::uint32_t sector,const std::vector<StorageRange>& after) {
    Value result(Json::arrayValue);
    for(const auto& range:gpt_regions(fd,sector)) {
        bool overlaps=false;
        for(const auto& changed:after)overlaps=overlaps || (changed.offset<range.offset+range.bytes.size() && range.offset<changed.offset+changed.bytes.size());
        if(!overlaps)result.append(descriptions({range})[0]);
    }
    return result;
}
void verify_descriptions(int fd,const Value& ranges) {
    for(const auto& range:ranges)require(sha256(storage_read(fd,range["offset"].asUInt64(),static_cast<std::size_t>(range["bytes"].asUInt64())))==range["sha256"].asString(),
        "changed-target","Protected GPT metadata changed outside this plan");
}
void protect_usable(int fd,const Value& ranges,std::uint32_t sector) {
    const auto table=gpt_inspect(fd,sector);
    if(table["disk_guid"].isNull())return; // same-image identity and full backup still apply.
    const auto& header=table[table["primary"]["valid"].asBool() ? "primary" : "backup"];
    const auto first=header["first_usable_lba"].asUInt64()*sector,last=(header["last_usable_lba"].asUInt64()+1)*sector;
    for(const auto& range:ranges)require(range["offset"].asUInt64()+range["bytes"].asUInt64()<=first || range["offset"].asUInt64()>=last,
        "layout-change-required","GPT metadata destination overlaps currently usable data; a separate layout/migration transaction is required");
}
Value proposed_table(const StorageTarget& target,const std::vector<StorageRange>& after) {
    Fd temporary(::memfd_create("ure-gpt-plan",MFD_CLOEXEC));
    require(temporary.get()>=0 && ::ftruncate(temporary.get(),static_cast<off_t>(target.identity["bytes"].asUInt64()))==0,
        "validation-unavailable","Cannot reconstruct proposed GPT metadata");
    const auto sector=target.identity["logical_sector_bytes"].asUInt();
    if(!gpt_inspect(target.descriptor.get(),sector)["disk_guid"].isNull())
        for(const auto& range:gpt_regions(target.descriptor.get(),sector))write_all(temporary.get(),range.bytes,range.offset);
    for(const auto& range:after)write_all(temporary.get(),range.bytes,range.offset);
    auto table=gpt_inspect(temporary.get(),sector);
    require(table["healthy"]==true,"invalid-gpt-plan","Proposed metadata does not produce two matching valid GPT copies"); return table;
}
std::vector<StorageRange> stock_desired(const StorageTarget& target,const fs::path& inputs,unsigned lun,
                                      const std::string& profile,const fs::path& identity_backup,Value& source) {
    require(target.identity["logical_sector_bytes"].isUInt() && target.identity["logical_sector_bytes"].asUInt()==4096,
        "invalid-sector","Pinned Uke stock metadata uses 4096-byte sectors");
    Value identities=gpt_inspect(target.descriptor.get(),4096),backup;
    if(!identity_backup.empty()) {
        auto store=private_directory(identity_backup,false); backup=backup_manifest(store); same_target(target.identity,backup["target_identity"]);
        require(backup["firmware_profile"]==profile,"wrong-profile","Original GPT backup has another firmware profile");
        identities=record(store,"partition-table.json");
    }
    auto ranges=gpt_stock_regions(inputs,target.identity["bytes"].asUInt64(),lun,identities,profile,source);
    // Unknown current partitions remain protected even if an older original
    // backup can supply all stock GUIDs. A separate migration must classify them.
    std::set<std::string> expected;
    for(const auto& part:source["desired_table"]["partitions"])expected.insert(part["label"].asString());
    for(const auto& part:source["desired_table"]["reserved_records"])expected.insert(part["label"].asString());
    const auto current=gpt_inspect(target.descriptor.get(),4096);
    for(const auto& part:current["partitions"]) {
        const auto name=part["label"].asString();
        require(expected.count(name)!=0 || name=="uke_linux" || name=="uke_windows" || name=="uke_esp",
            "protected-partition","Unknown current partitions cannot be removed by stock restoration");
    }
    source["identity_backup_manifest_sha256"]=backup["manifest_sha256"];
    source["manifest_sha256"]=seal(source,"manifest_sha256"); return ranges;
}
std::vector<StorageRange> plan_desired(const StorageTarget& target,const Value& plan,Value& source) {
    if(plan["operation"]=="gpt.layout")return gpt_layout_regions(target,plan["layout"]["request"],plan["firmware_profile"].asString(),source);
    if(plan["operation"]=="gpt.stock")return stock_desired(target,plan["stock_inputs_directory"].asString(),plan["stock_lun"].asUInt(),
        plan["firmware_profile"].asString(),plan["backup_directory"].asString(),source);
    return desired(target,plan["operation"].asString(),plan["backup_directory"].asString(),plan["firmware_profile"].asString(),source);
}
Fd lock_journal(const Root& journal) {
    auto lock=journal.open(".lock",O_RDWR|O_CREAT,0600); private_file(journal,".lock");
    require(::flock(lock.get(),LOCK_EX|LOCK_NB)==0,"busy-journal","Another process owns this GPT journal"); return lock;
}
void journal_binding(const Root& journal,const fs::path& path) {
    auto current=private_directory(path,false); struct stat retained{},now{};
    require(::fstat(journal.fd(),&retained)==0 && ::fstat(current.fd(),&now)==0 && retained.st_dev==now.st_dev && retained.st_ino==now.st_ino,
        "changed-journal","GPT journal path was replaced");
}
void boundary(const Root& journal,Value& state,const std::string& next) {
    state["state"]=next; state["timestamp_utc"]=utc(); journal.save_record("journal.json",state,true);
}
void verify_ranges(int fd,const std::vector<StorageRange>& ranges) {
    for(const auto& range:ranges)require(storage_read(fd,range.offset,range.bytes.size())==range.bytes,"verification-error","GPT range readback differs");
}
void write_gate(const StorageTarget& target) { storage_write_gate(target); }
void check_plan(const Value& plan) {
    require(plan["schema"]==1 && (plan["operation"]=="gpt.repair" || plan["operation"]=="gpt.restore" || plan["operation"]=="gpt.stock" || plan["operation"]=="gpt.layout") &&
        plan["target_identity"].isObject() && plan["target_identity"]["bytes"].isUInt64() &&
        plan["target_identity"]["logical_sector_bytes"].isUInt() && plan["firmware_profile"].isString() &&
        identifier(plan["firmware_profile"].asString()) && plan["operation_id"].isString() && identifier(plan["operation_id"].asString()) &&
        plan["plan_sha256"].isString() && hash_valid(plan["plan_sha256"].asString()) &&
        plan["plan_sha256"].asString()==seal(plan,"plan_sha256"),"invalid-gpt-plan","Invalid sealed GPT plan");
    const auto sector=plan["target_identity"]["logical_sector_bytes"].asUInt();
    require(sector==512 || sector==4096,"invalid-gpt-plan","Unsupported plan sector size");
    if(plan["operation"]=="gpt.layout")require(plan["layout"].isObject() && plan["layout"]["format"]=="ure-partition-layout" &&
        plan["layout"]["layout_sha256"].isString() && hash_valid(plan["layout"]["layout_sha256"].asString()) &&
        plan["layout"]["layout_sha256"].asString()==seal(plan["layout"],"layout_sha256") &&
        plan["layout"]["layout_sha256"]==plan["backup_manifest_sha256"] && plan["execution_scope"]=="GPT_METADATA_ONLY" &&
        plan["formats_filesystems"]==false && plan["migrates_data"]==false,"invalid-gpt-plan","Invalid scoped layout metadata plan");
    if(plan["operation"]=="gpt.stock")require(sector==4096 && plan["stock_lun"].isUInt() && plan["stock_lun"].asUInt()<6 &&
        plan["stock_inputs_directory"].isString() && fs::path(plan["stock_inputs_directory"].asString()).is_absolute() &&
        plan["backup_directory"].isString() && plan["stock_source"].isObject() && plan["stock_source"]["template_preview_only"]==false &&
        plan["stock_source"]["manifest_sha256"]==plan["backup_manifest_sha256"],"invalid-gpt-plan","Invalid original-identity-bound stock reconstruction plan");
    check_ranges(plan["before"],plan["target_identity"]["bytes"].asUInt64(),sector); check_ranges(plan["after"],plan["target_identity"]["bytes"].asUInt64(),sector);
    require(plan["before"].size()==plan["after"].size(),"invalid-gpt-plan","Original and target metadata range counts differ");
    for(Json::ArrayIndex i=0;i<plan["before"].size();++i)for(const auto* key:{"name","offset","bytes"})
        require(json(plan["before"][i][key])==json(plan["after"][i][key]),"invalid-gpt-plan","Original and target metadata geometries differ");
    if(!plan["untouched"].empty())check_ranges(plan["untouched"],plan["target_identity"]["bytes"].asUInt64(),sector);
    require(plan["untouched"].isArray(),"invalid-gpt-plan","Protected metadata inventory is missing");
}
int order(const std::string& name) {
    if(name=="backup_table")return 0;
    if(name=="backup_header")return 1;
    if(name=="primary_table")return 2;
    if(name=="primary_header")return 3;
    if(name=="protective_mbr")return 4;
    throw Error("invalid-gpt-plan","Unexpected GPT region name");
}
struct JournalReview {
    Value plan, state, result;
    std::vector<StorageRange> before;
};
JournalReview inspect_journal(const StorageTarget& target,const Root& journal,const Root* system,bool terminal_replay=false) {
    JournalReview review; review.plan=record(journal,"plan.json"); check_plan(review.plan);
    review.state=record(journal,"journal.json"); const auto& plan=review.plan;
    require(review.state["schema"]==1 && review.state["operation_id"]==plan["operation_id"] &&
        review.state["plan_sha256"]==plan["plan_sha256"] && review.state["state"].isString(),
        "invalid-journal","GPT journal is not bound to its persisted plan");
    const std::set<std::string> phases={"VALIDATED","BACKUP_STARTED","BACKUP_VERIFIED","READY","EXECUTING","VERIFYING","COMMITTED","FAILED_SAFE","FAILED_UNCERTAIN","ROLLBACK_REQUIRED","ROLLED_BACK"};
    require(phases.count(review.state["state"].asString())!=0,"invalid-journal","Unknown GPT journal phase");
    same_target(target.identity,plan["target_identity"]); storage_revalidate(target,system);
    protect_usable(target.descriptor.get(),plan["before"],target.identity["logical_sector_bytes"].asUInt());
    verify_descriptions(target.descriptor.get(),plan["untouched"]);
    bool original=true,payload=true,expected=true;
    Value regions(Json::arrayValue);
    for(Json::ArrayIndex index=0;index<plan["before"].size();++index) {
        const auto& range=plan["before"][index]; const auto name=range["name"].asString();
        private_file(journal,"before-"+name+".bin"); private_file(journal,"after-"+name+".bin");
        const auto before=journal.read("before-"+name+".bin",range_limit),after=journal.read("after-"+name+".bin",range_limit);
        require(before.size()==range["bytes"].asUInt64() && sha256(before)==range["sha256"].asString() &&
            after.size()==before.size() && sha256(after)==plan["after"][index]["sha256"].asString(),
            "backup-corrupt","GPT journal original or target metadata differs from its sealed plan");
        const auto current=storage_read(target.descriptor.get(),range["offset"].asUInt64(),before.size());
        const bool is_original=current==before,is_payload=current==after; bool is_expected=true;
        for(std::size_t i=0;i<current.size();++i)if(current[i]!=before[i] && current[i]!=after[i]) { is_expected=false; break; }
        original=original && is_original; payload=payload && is_payload; expected=expected && is_expected;
        Value item; item["name"]=name; item["original"]=is_original; item["payload"]=is_payload; item["expected_bytes_only"]=is_expected; regions.append(item);
        review.before.push_back({name,range["offset"].asUInt64(),before});
    }
    auto& result=review.result; result["schema"]=1; result["read_only"]=true; result["private_record"]=true;
    result["plan_sha256"]=plan["plan_sha256"]; result["state"]=review.state["state"]; result["target_identity"]=target.identity;
    result["regions"]=regions; result["classification"]=payload ? "TARGET_CONTENT_VERIFIED" : original ? "ORIGINAL_CONTENT_VERIFIED" : expected ? "PARTIAL_EXPECTED_WRITE" : "DIVERGED";
    result["current_table"]=gpt_inspect(target.descriptor.get(),target.identity["logical_sector_bytes"].asUInt());
    result["original_table"]=plan["current_table"]; result["desired_table"]=plan["desired_table"];
    result["recovery_actions"]=Value(Json::arrayValue);
    if(expected && target.identity["kind"]=="regular-image")result["recovery_actions"].append("rollback");
    const auto phase=review.state["state"].asString();
    if(payload && tables_equal(result["current_table"],plan["desired_table"]) &&
        (phase=="EXECUTING" || phase=="VERIFYING" || phase=="FAILED_UNCERTAIN" || (terminal_replay && phase=="COMMITTED")))result["recovery_actions"].append("resume");
    storage_revalidate(target,system); return review;
}
}
Value gpt_backup(const StorageTarget& target,const fs::path& directory,const std::string& profile,const Root* system) {
    require(identifier(profile),"invalid-profile","An explicit firmware profile is required");
    require(target.identity["kind"]=="regular-image" || target.identity["partition"]==false,"invalid-target","GPT backup selects a whole disk/LUN");
    Value request; request["operation"]="gpt.backup"; request["target_identity"]=target.identity; request["firmware_profile"]=profile;
    ManagedOperation operation(operation_binding("gpt.backup",request,directory,operation_targets(request["target_identity"])));
    storage_revalidate(target,system); const auto sector=target.identity["logical_sector_bytes"].asUInt();
    const auto inspection=gpt_inspect(target.descriptor.get(),sector); auto ranges=gpt_regions(target.descriptor.get(),sector);
    auto store=private_directory(directory,true); auto lock=lock_journal(store);
    operation.token().require_binding(operation_binding("gpt.backup",request,directory,operation_targets(request["target_identity"])));
    for(const auto& range:ranges)store_bytes(store,range.name+".bin",range.bytes);
    store.save_record("partition-table.json",inspection);
    storage_revalidate(target,system);
    require(json(descriptions(gpt_regions(target.descriptor.get(),sector)))==json(descriptions(ranges)),"stale-device","GPT changed during backup");
    Value manifest; manifest["schema"]=1; manifest["format"]="ure-gpt-backup"; manifest["target_identity"]=target.identity;
    manifest["firmware_profile"]=profile; manifest["firmware_identity_validated"]=false; manifest["created_utc"]=utc();
    manifest["tool"]="libuke-recovery/gpt-v1"; manifest["regions"]=descriptions(ranges); manifest["private_record"]=true;
    manifest["partition_table_sha256"]=sha256(json(inspection)); manifest["physical_test_record"]=false;
    manifest["manifest_sha256"]=seal(manifest,"manifest_sha256"); store.save_record("manifest.json",manifest);
    std::string sums;
    for(const auto& range:ranges)sums+=sha256(range.bytes)+"  "+range.name+".bin\n";
    sums+=manifest["partition_table_sha256"].asString()+"  partition-table.json\n"+sha256(json(manifest))+"  manifest.json\n";
    store_bytes(store,"SHA256SUMS",sums); backup_manifest(store); return manifest;
}
Value gpt_backup_verify(const fs::path& directory) {
    auto store=private_directory(directory,false); const auto manifest=backup_manifest(store);
    Value result; result["schema"]=1; result["verified"]=true; result["manifest_sha256"]=manifest["manifest_sha256"];
    result["target_identity"]=manifest["target_identity"]; result["firmware_profile"]=manifest["firmware_profile"];
    result["region_count"]=manifest["regions"].size(); result["private_record"]=true; return result;
}
Value gpt_compare(const StorageTarget& target,const fs::path& directory,const std::string& profile,const Root* system) {
    storage_revalidate(target,system);
    auto store=private_directory(directory,false); const auto manifest=backup_manifest(store); same_target(target.identity,manifest["target_identity"]);
    require(manifest["firmware_profile"]==profile,"wrong-profile","GPT backup firmware profile differs");
    Value result; result["schema"]=1; result["matches"]=true; result["read_only"]=true; result["private_record"]=true;
    result["ranges"]=Value(Json::arrayValue);
    for(const auto& range:load_ranges(store,manifest)) {
        Value item; item["name"]=range.name; item["matches"]=storage_read(target.descriptor.get(),range.offset,range.bytes.size())==range.bytes;
        if(item["matches"]==false)result["matches"]=false;
        result["ranges"].append(item);
    }
    storage_revalidate(target,system); return result;
}
Value gpt_plan(const StorageTarget& target,const std::string& operation,const std::string& profile,const fs::path& backup,const Root* system) {
    require(identifier(profile),"invalid-profile","An explicit firmware profile is required");
    storage_revalidate(target,system);
    require(target.identity["kind"]=="regular-image" || target.identity["partition"]==false,"invalid-target","GPT plans select a whole disk/LUN");
    Value source; const auto after=desired(target,operation,backup,profile,source); const auto before=read_original(target.descriptor.get(),after);
    protect_usable(target.descriptor.get(),descriptions(after),target.identity["logical_sector_bytes"].asUInt());
    Value plan; plan["schema"]=1; plan["operation"]=operation; plan["operation_id"]=operation_id(); plan["created_utc"]=utc();
    plan["target_identity"]=target.identity; plan["firmware_profile"]=profile; plan["before"]=descriptions(before); plan["after"]=descriptions(after);
    plan["untouched"]=operation=="gpt.repair" ? untouched_regions(target.descriptor.get(),target.identity["logical_sector_bytes"].asUInt(),after) : Value(Json::arrayValue);
    plan["backup_directory"]=backup.empty() ? "" : fs::absolute(backup).lexically_normal().string(); plan["backup_manifest_sha256"]=source["manifest_sha256"];
    plan["risk"]="MODIFIES_PARTITION_TABLE"; plan["backup_required"]=true; plan["private_record"]=true; plan["physical_test_record"]=false;
    plan["live_write_backend_ready"]=false; plan["cancel_semantics"]="cancel-before-executing; interrupted execution requires inspection/rollback";
    plan["current_table"]=gpt_inspect(target.descriptor.get(),target.identity["logical_sector_bytes"].asUInt());
    plan["desired_table"]=proposed_table(target,after);
    plan["plan_sha256"]=seal(plan,"plan_sha256"); check_plan(plan); storage_revalidate(target,system); return plan;
}
Value gpt_layout_plan(const StorageTarget& target,const Value& request,const std::string& profile,const Root* system) {
    storage_revalidate(target,system); Value source;
    const auto after=gpt_layout_regions(target,request,profile,source,system); const auto before=read_original(target.descriptor.get(),after);
    protect_usable(target.descriptor.get(),descriptions(after),target.identity["logical_sector_bytes"].asUInt());
    Value plan; plan["schema"]=1; plan["operation"]="gpt.layout"; plan["operation_id"]=operation_id(); plan["created_utc"]=utc();
    plan["target_identity"]=target.identity; plan["firmware_profile"]=profile; plan["before"]=descriptions(before); plan["after"]=descriptions(after);
    plan["untouched"]=Value(Json::arrayValue); plan["backup_directory"]=""; plan["backup_manifest_sha256"]=source["manifest_sha256"];
    source.removeMember("manifest_sha256"); plan["layout"]=source; plan["risk"]="MODIFIES_PARTITION_TABLE_AND_OS_VISIBILITY";
    plan["execution_scope"]="GPT_METADATA_ONLY"; plan["formats_filesystems"]=false; plan["migrates_data"]=false;
    plan["complete_partition_job"]=false; plan["backup_required"]=true; plan["private_record"]=true; plan["physical_test_record"]=false;
    plan["live_write_backend_ready"]=false; plan["cancel_semantics"]="cancel-before-executing; interrupted execution requires inspection/rollback";
    plan["current_table"]=gpt_inspect(target.descriptor.get(),target.identity["logical_sector_bytes"].asUInt()); plan["desired_table"]=proposed_table(target,after);
    plan["plan_sha256"]=seal(plan,"plan_sha256"); check_plan(plan); storage_revalidate(target,system); return plan;
}
Value gpt_stock_plan(const StorageTarget& target,const fs::path& inputs,unsigned lun,const std::string& profile,
                     const fs::path& identity_backup,const Root* system) {
    storage_revalidate(target,system);
    require(target.identity["kind"]=="regular-image" || target.identity["partition"]==false,"invalid-target","Stock GPT restoration selects a whole disk/LUN");
    Value source; const auto after=stock_desired(target,inputs,lun,profile,identity_backup,source);
    const auto before=read_original(target.descriptor.get(),after);
    protect_usable(target.descriptor.get(),descriptions(after),4096);
    Value plan; plan["schema"]=1; plan["operation"]="gpt.stock"; plan["operation_id"]=operation_id(); plan["created_utc"]=utc();
    plan["target_identity"]=target.identity; plan["firmware_profile"]=profile; plan["stock_lun"]=lun;
    plan["stock_inputs_directory"]=fs::absolute(inputs).lexically_normal().string(); plan["stock_source"]=source;
    plan["backup_directory"]=identity_backup.empty() ? "" : fs::absolute(identity_backup).lexically_normal().string();
    plan["backup_manifest_sha256"]=source["manifest_sha256"]; plan["before"]=descriptions(before); plan["after"]=descriptions(after);
    plan["untouched"]=Value(Json::arrayValue); plan["risk"]="MODIFIES_PARTITION_TABLE_AND_OS_VISIBILITY";
    plan["backup_required"]=true; plan["private_record"]=true; plan["physical_test_record"]=false;
    plan["live_write_backend_ready"]=false; plan["restores_partition_contents"]=false; plan["data_migration_performed"]=false;
    plan["cancel_semantics"]="cancel-before-executing; interrupted execution requires inspection/rollback";
    plan["current_table"]=gpt_inspect(target.descriptor.get(),4096); plan["desired_table"]=proposed_table(target,after);
    plan["layout_changes"]=Value(Json::arrayValue);
    std::map<std::string,Value> old,proposed;
    for(const auto& part:plan["current_table"]["partitions"])old.emplace(part["label"].asString(),part);
    for(const auto& part:plan["desired_table"]["partitions"])proposed.emplace(part["label"].asString(),part);
    for(const auto& part:plan["current_table"]["reserved_records"])old.emplace(part["label"].asString(),part);
    for(const auto& part:plan["desired_table"]["reserved_records"])proposed.emplace(part["label"].asString(),part);
    std::set<std::string> labels; for(const auto& [name,part]:old) { (void)part; labels.insert(name); }
    for(const auto& [name,part]:proposed) { (void)part; labels.insert(name); }
    for(const auto& name:labels) {
        const auto a=old.find(name),b=proposed.find(name);
        const Value original=a==old.end() ? Value() : a->second,changed=b==proposed.end() ? Value() : b->second;
        if(json(original)==json(changed))continue;
        Value item; item["label"]=name; item["before"]=original; item["after"]=changed;
        item["effect"]=changed.isNull() ? "REMOVED_FROM_TABLE_DATA_RETAINED" : original.isNull() ? "RESTORED_ENTRY_CONTENT_NOT_VERIFIED" : "METADATA_CHANGED_CONTENT_NOT_MOVED";
        plan["layout_changes"].append(item);
    }
    plan["plan_sha256"]=seal(plan,"plan_sha256"); check_plan(plan); storage_revalidate(target,system); return plan;
}
std::vector<StorageRange> gpt_stock_plan_regions(const StorageTarget& target,const Value& plan) {
    check_plan(plan); require(plan["operation"]=="gpt.stock","invalid-gpt-plan","Select a reviewed stock GPT plan");
    storage_revalidate(target); Value source; const auto after=plan_desired(target,plan,source);
    require(json(source)==json(plan["stock_source"]) && json(descriptions(after))==json(plan["after"]),
        "stale-plan","Stock source or desired GPT differs from its reviewed plan"); return after;
}
Value gpt_execute(StorageTarget& target,const Value& plan,const fs::path& directory,const std::string& confirmation,const Root* system) {
    check_plan(plan); require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the exact GPT plan checksum");
    require(json(target.identity)==json(plan["target_identity"]),"stale-plan","GPT target identity changed since planning");
    write_gate(target);
    ManagedOperation operation(operation_binding(plan["operation"].asString(),plan,directory,operation_targets(plan["target_identity"])));
    TargetLock target_lock(target.descriptor.get());
    storage_revalidate(target,system); Value source;
    auto after=plan_desired(target,plan,source);
    const auto before=read_original(target.descriptor.get(),after);
    require(json(descriptions(before))==json(plan["before"]) && json(descriptions(after))==json(plan["after"]) &&
        source["manifest_sha256"]==plan["backup_manifest_sha256"],"stale-plan","GPT metadata or restore source changed since planning");
    protect_usable(target.descriptor.get(),plan["after"],target.identity["logical_sector_bytes"].asUInt());
    require(tables_equal(proposed_table(target,after),plan["desired_table"]),"invalid-gpt-plan","Proposed GPT differs from the reviewed table");
    verify_descriptions(target.descriptor.get(),plan["untouched"]);
    auto journal=private_directory(directory,true); auto lock=lock_journal(journal);
    operation.token().require_binding(operation_binding(plan["operation"].asString(),plan,directory,operation_targets(plan["target_identity"])));
    journal.save_record("plan.json",plan);
    Value state; state["schema"]=1; state["operation_id"]=plan["operation_id"]; state["plan_sha256"]=plan["plan_sha256"];
    state["completed_ranges"]=Value(Json::arrayValue); state["private_record"]=true; boundary(journal,state,"VALIDATED");
    bool execution=false;
    try {
        struct statvfs space{}; std::uint64_t needed=65536+json(plan).size();
        for(const auto& range:before)needed+=range.bytes.size();
        for(const auto& range:after)needed+=range.bytes.size();
        require(::fstatvfs(journal.fd(),&space)==0 && space.f_frsize>0 && needed/space.f_frsize<space.f_bavail,
            "insufficient-space","GPT journal space cannot cover all original and target metadata");
        boundary(journal,state,"BACKUP_STARTED");
        for(const auto& range:before)store_bytes(journal,"before-"+range.name+".bin",range.bytes);
        for(const auto& range:after)store_bytes(journal,"after-"+range.name+".bin",range.bytes);
        boundary(journal,state,"BACKUP_VERIFIED"); verify_ranges(target.descriptor.get(),before); verify_descriptions(target.descriptor.get(),plan["untouched"]); storage_revalidate(target,system);
        boundary(journal,state,"READY");
        std::sort(after.begin(),after.end(),[](const auto& a,const auto& b){return order(a.name)<order(b.name);});
        boundary(journal,state,"EXECUTING");
        journal_binding(journal,directory);
        operation.token().require_binding(operation_binding(plan["operation"].asString(),plan,directory,operation_targets(plan["target_identity"])));
        operation.begin("GPT_WRITE_INTENT"); execution=true;
        for(const auto& range:after) {
            operation.token().require_active();
            write_all(target.descriptor.get(),range.bytes,range.offset); verify_ranges(target.descriptor.get(),{range});
            state["completed_ranges"].append(range.name); boundary(journal,state,"EXECUTING");
        }
        boundary(journal,state,"VERIFYING"); verify_ranges(target.descriptor.get(),after); verify_descriptions(target.descriptor.get(),plan["untouched"]);
        require(tables_equal(gpt_inspect(target.descriptor.get(),target.identity["logical_sector_bytes"].asUInt()),plan["desired_table"]),"verification-error","Resulting GPT differs from the reviewed table");
        state["verified"]=true;
        if(plan["operation"]=="gpt.layout") { state["execution_scope"]="GPT_METADATA_ONLY"; state["formats_filesystems"]=false; state["migrates_data"]=false; state["complete_partition_job"]=false; }
        journal_binding(journal,directory);
        operation.token().require_binding(operation_binding(plan["operation"].asString(),plan,directory,operation_targets(plan["target_identity"])));
        boundary(journal,state,"COMMITTED"); return operation.finish(state,true,true);
    } catch(const Error& error) {
        state["error_code"]=error.code; try { boundary(journal,state,execution ? "FAILED_UNCERTAIN" : "FAILED_SAFE"); } catch(...) {} throw;
    }
}
Value gpt_journal_inspect(const StorageTarget& target,const fs::path& directory,const Root* system) {
    auto journal=private_directory(directory,false); private_file(journal,".lock"); auto lock=journal.open(".lock",O_RDONLY);
    require(::flock(lock.get(),LOCK_SH|LOCK_NB)==0,"busy-journal","Another process owns this GPT journal");
    return inspect_journal(target,journal,system).result;
}
Value gpt_resume(const StorageTarget& target,const fs::path& directory,const std::string& confirmation,const Root* system) {
    auto journal=private_directory(directory,false); const auto plan=record(journal,"plan.json"); check_plan(plan);
    require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the original GPT plan checksum");
    ManagedOperation operation(operation_binding(plan["operation"].asString(),plan,directory,operation_targets(plan["target_identity"])),true);
    TargetLock target_lock(target.descriptor.get()); auto lock=lock_journal(journal); journal_binding(journal,directory);
    operation.token().require_binding(operation_binding(plan["operation"].asString(),plan,directory,operation_targets(plan["target_identity"])));
    require(json(record(journal,"plan.json"))==json(plan),"changed-journal","GPT recovery plan changed during ownership admission");
    auto review=inspect_journal(target,journal,system,operation.token().has_retained_intent());
    require(json(review.plan)==json(plan),"changed-journal","GPT recovery inspection selected another plan");
    bool allowed=false; for(const auto& action:review.result["recovery_actions"])allowed=allowed || action=="resume";
    require(allowed,"unsafe-resume","GPT resume only completes a verified target readback; it never replays interrupted writes");
    review.state["verified"]=true; review.state["resume_readback_only"]=true;
    journal_binding(journal,directory);
    operation.token().require_binding(operation_binding(plan["operation"].asString(),plan,directory,operation_targets(plan["target_identity"])));
    boundary(journal,review.state,"COMMITTED"); return operation.finish(review.state,true,true);
}
Value gpt_rollback(StorageTarget& target,const fs::path& directory,const std::string& confirmation,const Root* system) {
    auto journal=private_directory(directory,false); const auto plan=record(journal,"plan.json"); check_plan(plan);
    require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the original GPT plan checksum");
    write_gate(target);
    ManagedOperation operation(operation_binding(plan["operation"].asString(),plan,directory,operation_targets(plan["target_identity"])),true);
    TargetLock target_lock(target.descriptor.get());
    auto lock=lock_journal(journal); journal_binding(journal,directory);
    operation.token().require_binding(operation_binding(plan["operation"].asString(),plan,directory,operation_targets(plan["target_identity"])));
    require(json(record(journal,"plan.json"))==json(plan),"changed-journal","GPT recovery plan changed during ownership admission");
    auto review=inspect_journal(target,journal,system); auto state=review.state;
    require(json(review.plan)==json(plan),"changed-journal","GPT recovery inspection selected another plan");
    require(review.result["classification"]!="DIVERGED","changed-target","Unrelated GPT metadata changes prevent rollback");
    auto before=std::move(review.before);
    // A failed write can leave a partially updated table. Rollback restores only
    // the sealed metadata ranges on the same inode/device, capacity and GUID.
    // It never infers a safe target from the current /dev name alone.
    std::sort(before.begin(),before.end(),[](const auto& a,const auto& b){return order(a.name)<order(b.name);});
    boundary(journal,state,"ROLLBACK_REQUIRED");
    try {
        bool intent=false;
        for(const auto& range:before) {
            operation.token().require_active(); journal_binding(journal,directory);
            if(storage_read(target.descriptor.get(),range.offset,range.bytes.size())==range.bytes)continue;
            if(!intent) {
                operation.token().require_binding(operation_binding(plan["operation"].asString(),plan,directory,operation_targets(plan["target_identity"])));
                operation.begin("GPT_ROLLBACK_INTENT"); intent=true;
            }
            write_all(target.descriptor.get(),range.bytes,range.offset);
        }
        verify_ranges(target.descriptor.get(),before); verify_descriptions(target.descriptor.get(),plan["untouched"]);
        require(tables_equal(gpt_inspect(target.descriptor.get(),target.identity["logical_sector_bytes"].asUInt()),plan["current_table"]),
            "verification-error","Rolled-back GPT differs from the reviewed original table");
        journal_binding(journal,directory);
        operation.token().require_binding(operation_binding(plan["operation"].asString(),plan,directory,operation_targets(plan["target_identity"])));
        state["verified"]=true; boundary(journal,state,"ROLLED_BACK"); return operation.finish(state,true,true);
    } catch(const Error& error) { state["error_code"]=error.code; try { boundary(journal,state,"FAILED_UNCERTAIN"); } catch(...) {} throw; }
}
} // namespace ure
