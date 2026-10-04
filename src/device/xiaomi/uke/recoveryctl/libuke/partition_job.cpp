// SPDX-License-Identifier: Apache-2.0
// Prepare every filesystem before any original write. Journal disjoint userdata
// and GPT ranges; never rewrite an OEM payload even when its bytes are unchanged.
#include "uke.h"
#include "operation_guard.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <map>
#include <set>
#include <openssl/evp.h>
#include <linux/fs.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace ure {
namespace {
constexpr std::uint64_t mib=1048576,maximum=512ULL*1024*1024*1024,margin=64*mib;
constexpr unsigned maximum_chunks=16384;
std::string seal(Value value,const char* key) { value.removeMember(key); return sha256(json(value)); }
bool number(const Value& value,std::uint64_t expected) { return value.isUInt64() && value.asUInt64()==expected; }
struct Digest {
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> context{EVP_MD_CTX_new(),EVP_MD_CTX_free};
    Digest() { require(context && EVP_DigestInit_ex(context.get(),EVP_sha256(),nullptr)==1,"hash-error","Cannot initialize range hash"); }
    void add(std::string_view data) { require(EVP_DigestUpdate(context.get(),data.data(),data.size())==1,"hash-error","Cannot hash range"); }
    std::string finish() {
        std::array<unsigned char,32> bytes{}; unsigned size=0;
        require(EVP_DigestFinal_ex(context.get(),bytes.data(),&size)==1 && size==bytes.size(),"hash-error","Cannot finish range hash");
        constexpr char hex[]="0123456789abcdef"; std::string out; out.reserve(64);
        for(auto byte:bytes) { out+=hex[byte>>4]; out+=hex[byte&15]; } return out;
    }
};
std::string range_hash(int fd,std::uint64_t offset,std::uint64_t bytes) {
    Digest digest;
    while(bytes) { const auto size=static_cast<std::size_t>(std::min<std::uint64_t>(65536,bytes)); digest.add(storage_read(fd,offset,size)); offset+=size; bytes-=size; }
    return digest.finish();
}
void write(int fd,std::uint64_t offset,std::string_view bytes) {
    while(!bytes.empty()) { const auto n=::pwrite(fd,bytes.data(),bytes.size(),static_cast<off_t>(offset));
        if(n<0 && errno==EINTR)continue;
        require(n>0,"io-error","Bounded partition write failed"); offset+=static_cast<std::uint64_t>(n); bytes.remove_prefix(static_cast<std::size_t>(n)); }
}
void sync(int fd) { require(::fsync(fd)==0,"uncertain-write","Cannot synchronize partition transaction bytes"); }
void copy(int source,int destination,std::uint64_t offset,std::uint64_t bytes) {
    require(::ftruncate(destination,static_cast<off_t>(bytes))==0,"io-error","Cannot size private range image");
    struct file_clone_range clone{}; clone.src_fd=source; clone.src_offset=offset; clone.src_length=bytes;
    if(::ioctl(destination,FICLONERANGE,&clone)==0) { sync(destination); return; }
    for(std::uint64_t at=0;at<bytes;) {
        const auto data=storage_read(source,offset+at,static_cast<std::size_t>(std::min<std::uint64_t>(65536,bytes-at)));
        // The destination was newly created and truncated. Zero extents can
        // remain sparse; this optimization never skips original-target writes.
        if(std::any_of(data.begin(),data.end(),[](char byte) { return byte!=0; }))write(destination,at,data);
        at+=data.size();
    }
    sync(destination);
}
Fd private_file(const Root& store,const std::string& file,std::uint64_t expected_bytes=UINT64_MAX) {
    auto fd=store.open(file,O_RDONLY|O_NONBLOCK); struct stat st{};
    require(::fstat(fd.get(),&st)==0 && S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==::geteuid() && (st.st_mode&07777)==0600 &&
        st.st_size>=0 && (expected_bytes==UINT64_MAX || static_cast<std::uint64_t>(st.st_size)==expected_bytes),
        "unsafe-partition-journal","Partition journal files must be owned, private, single-link regular files with exact sizes"); return fd;
}
Value record(const Root& store,const std::string& file) { auto fd=private_file(store,file); return parse_json(store.read(file,4*1024*1024)); }
Fd journal_lock(const Root& store) {
    auto fd=store.open("operation.lock",O_RDWR|O_CREAT,0600); struct stat st{};
    require(::fstat(fd.get(),&st)==0 && S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==::geteuid() && (st.st_mode&07777)==0600 &&
        ::flock(fd.get(),LOCK_EX|LOCK_NB)==0,"operation-busy","Partition journal is unsafe or in use"); return fd;
}
Fd inspection_lock(const Root& store) {
    auto fd=private_file(store,"operation.lock",0);
    require(::flock(fd.get(),LOCK_SH|LOCK_NB)==0,"operation-busy","A partition writer owns this journal"); return fd;
}
struct TargetLock {
    int fd;
    explicit TargetLock(int value) : fd(value) { require(::flock(fd,LOCK_EX|LOCK_NB)==0,"busy-target","A cooperating operation owns the disk image"); }
    ~TargetLock() { ::flock(fd,LOCK_UN); }
    TargetLock(const TargetLock&)=delete;
    TargetLock& operator=(const TargetLock&)=delete;
};
void binding(const StorageTarget& target,const Value& expected) {
    require(expected["kind"]=="regular-image" && target.identity["kind"]=="regular-image","firmware-unverified","Combined live repartitioning requires accepted device firmware, FBE and UFS ownership evidence");
    const auto current=storage_image(expected["path"].asString(),expected["logical_sector_bytes"].asUInt());
    for(const auto* key:{"path","file_device","file_inode","bytes","logical_sector_bytes","mode","uid","gid"})
        require(json(current.identity[key])==json(expected[key]) && json(target.identity[key])==json(expected[key]),"wrong-target","Partition job target path, inode, size or ownership differs");
    struct stat st{};
    require(::fstat(target.descriptor.get(),&st)==0 && S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_size>=0 &&
        static_cast<std::uint64_t>(st.st_dev)==expected["file_device"].asUInt64() && static_cast<std::uint64_t>(st.st_ino)==expected["file_inode"].asUInt64() &&
        static_cast<std::uint64_t>(st.st_size)==expected["bytes"].asUInt64() && st.st_uid==expected["uid"].asUInt() && st.st_gid==expected["gid"].asUInt() &&
        static_cast<unsigned>(st.st_mode&07777)==expected["mode"].asUInt(),"wrong-target","Retained partition target descriptor changed");
}
void journal_binding(const Root& store,const fs::path& path) {
    auto current=private_directory(path,false); struct stat old{},now{};
    require(::fstat(store.fd(),&old)==0 && ::fstat(current.fd(),&now)==0 && old.st_dev==now.st_dev && old.st_ino==now.st_ino,
        "changed-journal","Partition journal path was replaced");
}
std::string filesystem(const Value& row) { return row["filesystem"]=="fat32" ? "vfat" : row["filesystem"].asString(); }
int gpt_order(const std::string& name) {
    if(name=="backup_table")return 0;
    if(name=="backup_header")return 1;
    if(name=="primary_table")return 2;
    if(name=="primary_header")return 3;
    if(name=="protective_mbr")return 4;
    throw Error("invalid-partition-plan","Unknown GPT region role");
}
struct Range { std::uint64_t offset,bytes,before_offset; std::string before_file,after_file,name; };
std::vector<Range> ranges(const Value& plan) {
    std::vector<Range> out; const auto& layout=plan["gpt"]["layout"]; const auto begin=layout["pool"]["offset"].asUInt64();
    std::uint64_t cursor=begin;
    for(const auto& row:layout["rows"])if(row["enabled"]==true) {
        const auto size=row["bytes"].asUInt64(); const auto role=row["role"].asString();
        const auto after=role=="userdata" && row["action"]=="UNCHANGED_GEOMETRY" && layout["userdata_policy"]=="preserve" ?
            "userdata-before.img" : "stage-"+role+"/working.img";
        out.push_back({cursor,size,cursor-begin,"userdata-before.img",after,role}); cursor+=size;
    }
    const auto end=begin+layout["pool"]["original_bytes"].asUInt64();
    if(cursor<end)out.push_back({cursor,end-cursor,cursor-begin,"userdata-before.img","unallocated-after.img","unallocated"});
    std::vector<Value> metadata; for(const auto& range:plan["gpt"]["after"])metadata.push_back(range);
    std::sort(metadata.begin(),metadata.end(),[](const Value& a,const Value& b) { return gpt_order(a["name"].asString())<gpt_order(b["name"].asString()); });
    for(const auto& range:metadata) { const auto name=range["name"].asString();
        out.push_back({range["offset"].asUInt64(),range["bytes"].asUInt64(),0,"gpt-before-"+name+".bin","gpt-after-"+name+".bin",name}); }
    return out;
}
std::vector<std::pair<std::uint64_t,std::uint64_t>> protected_ranges(const Value& plan) {
    const auto& layout=plan["gpt"]["layout"]; std::vector<std::pair<std::uint64_t,std::uint64_t>> used;
    const auto start=layout["pool"]["offset"].asUInt64(); used.emplace_back(start,start+layout["pool"]["original_bytes"].asUInt64());
    for(const auto& range:plan["gpt"]["before"])used.emplace_back(range["offset"].asUInt64(),range["offset"].asUInt64()+range["bytes"].asUInt64());
    std::sort(used.begin(),used.end()); std::uint64_t cursor=0; std::vector<std::pair<std::uint64_t,std::uint64_t>> out;
    for(const auto& [begin,end]:used) { require(cursor<=begin,"invalid-partition-plan","Partition and GPT write ranges overlap"); if(cursor<begin)out.emplace_back(cursor,begin-cursor); cursor=end; }
    const auto capacity=plan["target_identity"]["bytes"].asUInt64(); require(cursor<=capacity,"invalid-partition-plan","Write ranges exceed capacity");
    if(cursor<capacity)out.emplace_back(cursor,capacity-cursor);
    return out;
}
void check_plan(const Value& plan) {
    require(plan["schema"]==1 && plan["operation"]=="partition.apply-layout" && identifier(plan["operation_id"].asString()) && identifier(plan["firmware_profile"].asString()) &&
        hash_valid(plan["plan_sha256"].asString()) && seal(plan,"plan_sha256")==plan["plan_sha256"].asString() && hash_valid(plan["userdata_sha256"].asString()) &&
        plan["live_write_backend_ready"]==false && plan["physical_test_record"]==false,"invalid-partition-plan","Invalid sealed partition job");
    const auto& gpt=plan["gpt"]; const auto& layout=gpt["layout"]; const auto capacity=plan["target_identity"]["bytes"].asUInt64();
    const auto sector=plan["target_identity"]["logical_sector_bytes"].asUInt();
    require(gpt["operation"]=="gpt.layout" && gpt["plan_sha256"]==seal(gpt,"plan_sha256") && json(gpt["target_identity"])==json(plan["target_identity"]) &&
        gpt["firmware_profile"]==plan["firmware_profile"] && gpt["current_table"]["healthy"]==true && gpt["desired_table"]["healthy"]==true &&
        layout["format"]=="ure-partition-layout" && layout["pool"]["source"]=="ORIGINAL_USERDATA_ONLY" && layout["rows"].isArray() && layout["rows"].size()==4 &&
        (sector==512 || sector==4096) && capacity<=INT64_MAX,"invalid-partition-plan","Partition job source table or scope is invalid");
    const auto start=layout["pool"]["offset"].asUInt64(),bytes=layout["pool"]["original_bytes"].asUInt64();
    require(bytes>=32*mib && bytes<=maximum && start<=capacity && bytes<=capacity-start && start%mib==0,"invalid-partition-plan","Invalid original userdata geometry");
    require(plan["estimated_journal_bytes"].isUInt64() && plan["estimated_journal_bytes"].asUInt64()==bytes*3+margin,"invalid-partition-plan","Invalid partition journal space estimate");
    require(plan["chunk_bytes"].isUInt64() && plan["chunk_bytes"].asUInt64()>=mib && plan["chunk_bytes"].asUInt64()<=64*mib &&
        plan["chunk_bytes"].asUInt64()%mib==0,"invalid-partition-plan","Invalid bounded partition chunk size");
    std::set<std::string> roles; std::uint64_t cursor=start;
    for(const auto& row:layout["rows"]) {
        const auto role=row["role"].asString(); const auto size=row["bytes"].asUInt64();
        require((role=="esp" || role=="linux" || role=="windows" || role=="userdata") && roles.insert(role).second && number(row["offset"],cursor) &&
            size<=start+bytes-cursor && (row["enabled"]==true)==(size!=0),"invalid-partition-plan","Partition roles overlap or escape original userdata");
        if(size)require(size>=32*mib && number(row["start_lba"],cursor/sector) && number(row["end_lba"],(cursor+size)/sector-1),"invalid-partition-plan","Partition geometry differs from its row");
        cursor+=size;
    }
    for(const auto& edit:layout["advanced_record_edits"])require(edit["payload_work_required"]==false,"advanced-content-workflow-required","OEM content changes require a separate explicit range transaction; this job writes userdata contents only");
    require(gpt["before"].isArray() && gpt["before"].size()==5 && gpt["after"].isArray() && gpt["after"].size()==5,"invalid-partition-plan","All five GPT regions are required");
    std::set<std::string> names;
    for(const auto& after:gpt["after"]) {
        const auto name=after["name"].asString(); const auto rank=gpt_order(name); const auto offset=after["offset"].asUInt64(),size=after["bytes"].asUInt64(); Value before;
        for(const auto& old:gpt["before"])if(old["name"]==name) { require(before.isNull(),"invalid-partition-plan","Duplicate original GPT role"); before=old; }
        require(names.insert(name).second && before.isObject() && before["offset"]==after["offset"] && before["bytes"]==after["bytes"] &&
            hash_valid(before["sha256"].asString()) && hash_valid(after["sha256"].asString()) && size>0 && size<=4*mib && offset%sector==0 && size%sector==0 &&
            offset<=capacity && size<=capacity-offset,"invalid-partition-plan","GPT region identity or boundary changed");
        require(rank==0 || rank==2 ? size>=16384 && offset>sector : size==sector && offset==(rank==1 ? capacity-sector : rank==3 ? sector : 0),"invalid-partition-plan","GPT region position is invalid");
    }
    const auto untouched=protected_ranges(plan); require(plan["protected"].isArray() && plan["protected"].size()==untouched.size(),"invalid-partition-plan","Protected-range coverage is incomplete");
    for(std::size_t i=0;i<untouched.size();++i) { const auto& row=plan["protected"][static_cast<Json::ArrayIndex>(i)]; require(number(row["offset"],untouched[i].first) &&
        number(row["bytes"],untouched[i].second) && hash_valid(row["sha256"].asString()),"invalid-partition-plan","Protected-range geometry or digest is invalid"); }
}
void protected_verify(const StorageTarget& target,const Value& plan) {
    for(const auto& range:plan["protected"])require(range_hash(target.descriptor.get(),range["offset"].asUInt64(),range["bytes"].asUInt64())==range["sha256"].asString(),
        "protected-range-changed","Bytes outside original userdata and GPT differ; automatic writes are refused");
}
void original_verify(const StorageTarget& target,const Value& plan) {
    binding(target,plan["target_identity"]); const auto& pool=plan["gpt"]["layout"]["pool"];
    require(range_hash(target.descriptor.get(),pool["offset"].asUInt64(),pool["original_bytes"].asUInt64())==plan["userdata_sha256"].asString(),"stale-source","Userdata changed after review");
    for(const auto& range:plan["gpt"]["before"])require(range_hash(target.descriptor.get(),range["offset"].asUInt64(),range["bytes"].asUInt64())==range["sha256"].asString(),"stale-source","Original GPT changed after review");
    protected_verify(target,plan); binding(target,plan["target_identity"]);
}
void phase(const Root& store,Value& state,const std::string& value) { state["state"]=value; state["timestamp_utc"]=utc(); store.save_record("state.json",state,true); }
bool encryption_feature(int fd,std::uint64_t offset,std::uint64_t bytes,const Value& signature) {
    if(signature["filesystem_encryption_feature"]!=false)return true;
    if(signature["type"]=="f2fs") {
        if(bytes<8192)return true;
        const auto backup=storage_read(fd,offset+5120,2184); auto word=[&](std::size_t at) { std::uint32_t out=0;
            for(unsigned i=0;i<4;++i)out|=static_cast<std::uint32_t>(static_cast<unsigned char>(backup[at+i]))<<(i*8);
            return out; };
        if(word(0)!=0xf2f52010U || (word(2180)&1U)!=0)return true;
    }
    return false;
}
void prepare(const Root& system,StorageTarget& target,const Root& store,const fs::path& path,const Value& plan,Value& state,OperationLease& parent) {
    const auto& pool=plan["gpt"]["layout"]["pool"]; const auto original_bytes=pool["original_bytes"].asUInt64();
    auto before=store.open("userdata-before.img",O_RDWR|O_CREAT|O_EXCL,0600);
    copy(target.descriptor.get(),before.get(),pool["offset"].asUInt64(),original_bytes);
    require(sha256(before.get())==plan["userdata_sha256"].asString(),"stale-source","Userdata staging readback differs from review");
    Value source; const auto desired=gpt_layout_regions(target,plan["gpt"]["layout"]["request"],plan["firmware_profile"].asString(),source,&system);
    require(source["layout_sha256"]==plan["gpt"]["layout"]["layout_sha256"],"stale-plan","Resolved partition layout differs from review");
    for(const auto& range:desired) {
        auto old=store.open("gpt-before-"+range.name+".bin",O_RDWR|O_CREAT|O_EXCL,0600); auto next=store.open("gpt-after-"+range.name+".bin",O_RDWR|O_CREAT|O_EXCL,0600);
        write(old.get(),0,storage_read(target.descriptor.get(),range.offset,range.bytes.size())); write(next.get(),0,range.bytes); sync(old.get()); sync(next.get());
    }
    sync(store.fd()); original_verify(target,plan); phase(store,state,"BEFORE_VERIFIED");
    for(const auto& row:plan["gpt"]["layout"]["rows"])if(row["enabled"]==true) {
        const auto role=row["role"].asString(),type=filesystem(row); const auto bytes=row["bytes"].asUInt64();
        if(role=="userdata" && row["action"]=="UNCHANGED_GEOMETRY" && plan["gpt"]["layout"]["userdata_policy"]=="preserve")continue;
        const bool shrinking=role=="userdata" && plan["gpt"]["layout"]["userdata_policy"]=="preserve";
        const auto seed_name="seed-"+role+".img";
        if(!shrinking) { auto seed=store.open(seed_name,O_RDWR|O_CREAT|O_EXCL,0600); require(::ftruncate(seed.get(),static_cast<off_t>(bytes))==0,"io-error","Cannot size new filesystem stage"); sync(seed.get()); sync(store.fd()); }
        auto seed=storage_image(path/(shrinking ? "userdata-before.img" : seed_name),512);
        Value request; request["schema"]=1; request["action"]=shrinking ? "resize" : "format"; request["filesystem"]=type;
        if(shrinking)request["target_bytes"]=Json::UInt64(bytes);
        else { request["erase_confirmed"]=true; request["label"]=role=="userdata" ? "USERDATA" : "URE_"+role; }
        const auto fs_plan=filesystem_operation_plan(system,seed,request,plan["firmware_profile"].asString());
        filesystem_prepare(system,seed,fs_plan,path/("stage-"+role),fs_plan["plan_sha256"].asString(),&parent);
        auto prepared=store.open("stage-"+role+"/working.img",O_RDWR); require(::ftruncate(prepared.get(),static_cast<off_t>(bytes))==0,"io-error","Cannot set final filesystem capacity"); sync(prepared.get());
        auto readonly=private_file(store,"stage-"+role+"/working.img",bytes); const auto signature=filesystem_probe(readonly.get()); const auto check=filesystem_check(readonly.get());
        require(signature["type"]==type && check["successful"]==true,"filesystem-post-check-failed","Filesystem does not pass its checker inside the final partition size");
        if(shrinking && plan["userdata_signature"]["uuid"].isString())require(signature["uuid"]==plan["userdata_signature"]["uuid"],"filesystem-uuid-changed","Userdata shrink must preserve its filesystem UUID");
        Value checked; checked["signature"]=signature; checked["check"]=check; checked["bytes"]=Json::UInt64(bytes); checked["sha256"]=sha256(readonly.get()); store.save_record("checked-"+role+".json",checked);
        if(!shrinking) { require(::unlinkat(store.fd(),seed_name.c_str(),0)==0,"io-error","Cannot remove completed zero seed"); sync(store.fd()); }
    }
    const auto geometry=ranges(plan); Value manifest; manifest["schema"]=1; manifest["format"]="ure-partition-application"; manifest["plan_sha256"]=plan["plan_sha256"]; manifest["chunks"]=Value(Json::arrayValue);
    for(const auto& range:geometry) {
        if(range.name=="unallocated") { auto zero=store.open(range.after_file,O_RDWR|O_CREAT|O_EXCL,0600); require(::ftruncate(zero.get(),static_cast<off_t>(range.bytes))==0,"io-error","Cannot size reviewed empty tail"); sync(zero.get()); }
        const auto old_size=range.before_file=="userdata-before.img" ? original_bytes : range.bytes;
        auto old=private_file(store,range.before_file,old_size),next=private_file(store,range.after_file,range.after_file=="userdata-before.img" ? original_bytes : range.bytes);
        for(std::uint64_t at=0;at<range.bytes;) {
            const auto bytes=std::min<std::uint64_t>(plan["chunk_bytes"].asUInt64(),range.bytes-at); Value chunk;
            chunk["name"]=range.name; chunk["offset"]=Json::UInt64(range.offset+at); chunk["bytes"]=Json::UInt64(bytes);
            chunk["before_file"]=range.before_file; chunk["before_offset"]=Json::UInt64(range.before_offset+at);
            chunk["after_file"]=range.after_file; chunk["after_offset"]=Json::UInt64(range.after_file=="userdata-before.img" ? range.before_offset+at : at);
            chunk["before_sha256"]=range_hash(old.get(),chunk["before_offset"].asUInt64(),bytes); chunk["after_sha256"]=range_hash(next.get(),chunk["after_offset"].asUInt64(),bytes);
            manifest["chunks"].append(chunk); at+=bytes;
        }
    }
    require(manifest["chunks"].size()<=maximum_chunks,"size-limit","Too many bounded partition chunks");
    manifest["manifest_sha256"]=seal(manifest,"manifest_sha256"); store.save_record("application.json",manifest); sync(store.fd()); original_verify(target,plan);
    state["manifest_sha256"]=manifest["manifest_sha256"]; phase(store,state,"READY");
}
struct Review { Value plan,state,manifest,result; std::vector<std::string> current_hashes; };
bool action(const Value& review,const std::string& name) { for(const auto& item:review["recovery_actions"])if(item==name)return true; return false; }
Review inspect(const StorageTarget& target,const Root& store,bool terminal_replay=false) {
    Review review; review.plan=record(store,"plan.json"); check_plan(review.plan); const auto& plan=review.plan;
    binding(target,plan["target_identity"]); protected_verify(target,plan); review.state=record(store,"state.json");
    require(review.state["plan_sha256"]==plan["plan_sha256"] && review.state["state"].isString(),"invalid-partition-journal","Partition state has a different plan binding");
    auto& result=review.result; result["operation"]=plan["operation"]; result["plan_sha256"]=plan["plan_sha256"]; result["state"]=review.state["state"];
    result["recovery_actions"]=Value(Json::arrayValue); result["physical_test_record"]=false; result["private_record"]=true; result["live_write_backend_ready"]=false;
    result["read_only"]=true;
    result["protected_ranges_verified"]=true; result["cooperating_locks_only"]=true; result["chunks"]=Value(Json::arrayValue);
    if(!store.exists("application.json")) { original_verify(target,plan); result["classification"]="ORIGINAL"; result["before_and_after_verified"]=false;
        if(review.state["state"]!="CANCELLED_SAFE" || terminal_replay)result["recovery_actions"].append("cancel");
        return review; }
    review.manifest=record(store,"application.json"); const auto& manifest=review.manifest;
    require(manifest["schema"]==1 && manifest["format"]=="ure-partition-application" && manifest["plan_sha256"]==plan["plan_sha256"] &&
        manifest["manifest_sha256"]==seal(manifest,"manifest_sha256") && manifest["chunks"].isArray() && manifest["chunks"].size()<=maximum_chunks,
        "invalid-partition-journal","Invalid sealed bounded application manifest");
    const auto original_bytes=plan["gpt"]["layout"]["pool"]["original_bytes"].asUInt64();
    auto original=private_file(store,"userdata-before.img",original_bytes); require(sha256(original.get())==plan["userdata_sha256"].asString(),"backup-corrupt","Complete original userdata backup differs");
    for(const auto* side:{"before","after"})for(const auto& range:plan["gpt"][side]) {
        auto file=private_file(store,"gpt-"+std::string(side)+"-"+range["name"].asString()+".bin",range["bytes"].asUInt64());
        require(sha256(file.get())==range["sha256"].asString(),"backup-corrupt","GPT recovery bytes differ from the reviewed table");
    }
    Json::ArrayIndex index=0; bool all_old=true,all_new=true,expected=true; std::uint64_t desired_bytes=0;
    for(const auto& range:ranges(plan)) {
        auto old=private_file(store,range.before_file,range.before_file=="userdata-before.img" ? original_bytes : range.bytes);
        auto next=private_file(store,range.after_file,range.after_file=="userdata-before.img" ? original_bytes : range.bytes);
        for(std::uint64_t at=0;at<range.bytes;) {
            require(index<manifest["chunks"].size(),"invalid-partition-journal","Application manifest omits a write range"); const auto& chunk=manifest["chunks"][index];
            const auto bytes=std::min<std::uint64_t>(plan["chunk_bytes"].asUInt64(),range.bytes-at);
            const auto after_offset=range.after_file=="userdata-before.img" ? range.before_offset+at : at;
            require(chunk["name"]==range.name && number(chunk["offset"],range.offset+at) && number(chunk["bytes"],bytes) &&
                chunk["before_file"]==range.before_file && number(chunk["before_offset"],range.before_offset+at) && chunk["after_file"]==range.after_file &&
                number(chunk["after_offset"],after_offset) && hash_valid(chunk["before_sha256"].asString()) && hash_valid(chunk["after_sha256"].asString()),
                "invalid-partition-journal","Application chunk escapes its exact userdata or GPT boundary");
            require(range_hash(old.get(),range.before_offset+at,bytes)==chunk["before_sha256"].asString() && range_hash(next.get(),after_offset,bytes)==chunk["after_sha256"].asString(),"backup-corrupt","Partition recovery mirror changed");
            const auto current=range_hash(target.descriptor.get(),range.offset+at,bytes); const bool was=current==chunk["before_sha256"].asString(),will=current==chunk["after_sha256"].asString(); bool known=was || will;
            if(!known) { known=true;
                for(std::uint64_t position=0;position<bytes && known;) {
                    const auto size=static_cast<std::size_t>(std::min<std::uint64_t>(65536,bytes-position)); const auto before=storage_read(old.get(),range.before_offset+at+position,size);
                    const auto after=storage_read(next.get(),after_offset+position,size),now=storage_read(target.descriptor.get(),range.offset+at+position,size);
                    for(std::size_t j=0;j<size;++j)if(now[j]!=before[j] && now[j]!=after[j]) { known=false; break; }
                    position+=size;
                }
            }
            all_old=all_old && was; all_new=all_new && will; expected=expected && known; if(will)desired_bytes+=bytes; review.current_hashes.push_back(current);
            if(index<128) { Value row; row["index"]=index; row["role"]=range.name; row["classification"]=will ? "TARGET" : was ? "ORIGINAL" : known ? "PARTIAL_EXPECTED_WRITE" : "DIVERGED"; result["chunks"].append(row); }
            ++index; at+=bytes;
        }
    }
    require(index==manifest["chunks"].size(),"invalid-partition-journal","Application manifest contains extra write ranges");
    result["classification"]=all_new ? "TARGET" : all_old ? "ORIGINAL" : expected ? "PARTIAL_EXPECTED_WRITE" : "DIVERGED";
    result["before_and_after_verified"]=true; result["chunk_count"]=index; result["chunks_truncated"]=index>128; result["verified_target_bytes"]=Json::UInt64(desired_bytes);
    const auto state=review.state["state"].asString(); const bool terminal=state=="CANCELLED_SAFE" || state=="ROLLED_BACK";
    if(all_old && (!terminal || (terminal_replay && state=="CANCELLED_SAFE")) && state!="COMMITTED")result["recovery_actions"].append("cancel");
    if(expected && (!terminal || (terminal_replay && state=="ROLLED_BACK" && all_old)))result["recovery_actions"].append("rollback");
    if(expected && !terminal && (state!="COMMITTED" || (terminal_replay && all_new)) && review.state.get("direction","apply")=="apply")result["recovery_actions"].append("resume");
    binding(target,plan["target_identity"]); protected_verify(target,plan); return review;
}
Value apply(StorageTarget& target,const Root& store,const fs::path& path,Review review,bool rollback,ManagedOperation& operation) {
    auto state=review.state; state["direction"]=rollback ? "rollback" : "apply"; state["written_bytes"]=Json::UInt64(0); state["verified"]=false; state.removeMember("error_code");
    bool intent=false;
    try {
        phase(store,state,rollback ? "ROLLBACK_REQUIRED" : "APPLYING");
        for(Json::ArrayIndex i=0;i<review.manifest["chunks"].size();++i) {
            operation.token().require_active();
            journal_binding(store,path); binding(target,review.plan["target_identity"]); storage_write_gate(target); const auto& chunk=review.manifest["chunks"][i];
            const auto offset=chunk["offset"].asUInt64(),bytes=chunk["bytes"].asUInt64(); const auto wanted=chunk[rollback ? "before_sha256" : "after_sha256"].asString();
            const auto now=range_hash(target.descriptor.get(),offset,bytes); require(now==review.current_hashes[i],"changed-target","Partition bytes changed after journal inspection");
            if(now!=wanted) {
                auto source=private_file(store,chunk[rollback ? "before_file" : "after_file"].asString()); const auto source_offset=chunk[rollback ? "before_offset" : "after_offset"].asUInt64();
                require(range_hash(source.get(),source_offset,bytes)==wanted,"backup-corrupt","Recovery input changed before writing");
                // Persist intent before the first byte. Progress never substitutes
                // for readback when recovering a forced reboot or partial write.
                state["active_chunk"]=i; phase(store,state,rollback ? "ROLLBACK_REQUIRED" : "APPLYING");
                for(std::uint64_t at=0;at<bytes;) {
                    const auto data=storage_read(source.get(),source_offset+at,static_cast<std::size_t>(std::min<std::uint64_t>(65536,bytes-at)));
                    if(!intent) {
                        operation.token().require_binding(operation_binding(review.plan["operation"].asString(),review.plan,path,operation_targets(review.plan["target_identity"])));
                        operation.begin(rollback ? "PARTITION_ROLLBACK_INTENT" : "PARTITION_WRITE_INTENT"); intent=true;
                    }
                    write(target.descriptor.get(),offset+at,data); at+=data.size();
                }
                sync(target.descriptor.get()); require(range_hash(target.descriptor.get(),offset,bytes)==wanted,"verification-error","Partition chunk readback differs");
                state["written_bytes"]=Json::UInt64(state["written_bytes"].asUInt64()+bytes);
            }
            state.removeMember("active_chunk"); state["last_verified_chunk"]=i; phase(store,state,rollback ? "ROLLBACK_REQUIRED" : "APPLYING");
        }
        phase(store,state,"VERIFYING"); journal_binding(store,path); binding(target,review.plan["target_identity"]); protected_verify(target,review.plan);
        for(const auto& chunk:review.manifest["chunks"])require(range_hash(target.descriptor.get(),chunk["offset"].asUInt64(),chunk["bytes"].asUInt64())==chunk[rollback ? "before_sha256" : "after_sha256"].asString(),"verification-error","Complete partition transaction readback differs");
        const auto table=gpt_inspect(target.descriptor.get(),review.plan["target_identity"]["logical_sector_bytes"].asUInt());
        require(table["healthy"]==true && json(table)==json(review.plan["gpt"][rollback ? "current_table" : "desired_table"]),"verification-error","Final GPT copies differ from the reviewed layout");
        state["verified"]=true; state["protected_ranges_verified"]=true; state["complete_partition_job"]=!rollback; state["physical_test_record"]=false;
        operation.token().require_binding(operation_binding(review.plan["operation"].asString(),review.plan,path,operation_targets(review.plan["target_identity"])));
        phase(store,state,rollback ? "ROLLED_BACK" : "COMMITTED"); target.identity=storage_image(target.identity["path"].asString(),target.identity["logical_sector_bytes"].asUInt()).identity;
        return operation.finish(state,true,true);
    } catch(const Error& error) { state["error_code"]=error.code; state["verified"]=false; try { phase(store,state,"RECOVERY_REQUIRED"); } catch(...) {} throw; }
}
} // namespace

Value partition_job_plan(const Root& system,const StorageTarget& target,const Value& request,const std::string& profile) {
    auto metadata=gpt_layout_plan(target,request,profile,&system); const auto& layout=metadata["layout"]; const auto& pool=layout["pool"];
    const auto bytes=pool["original_bytes"].asUInt64(); require(bytes>=32*mib && bytes<=maximum,"unsupported-filesystem-size","Combined image jobs require 32 MiB to 512 GiB userdata");
    const auto signature=filesystem_probe_range(target.descriptor.get(),pool["offset"].asUInt64(),bytes); const auto capabilities=filesystem_capabilities();
    for(const auto& row:layout["rows"])if(row["enabled"]==true) {
        require(row["bytes"].asUInt64()>=32*mib,"unsupported-filesystem-size","Every enabled filesystem requires at least 32 MiB");
        const auto type=filesystem(row); const bool preserve=row["role"]=="userdata" && layout["userdata_policy"]=="preserve";
        if(preserve) {
            require(signature["type"]==type && (type=="ext4" || type=="f2fs"),"userdata-filesystem-mismatch","Preserved userdata must match a supported ext4 or F2FS filesystem");
            require(signature["encryption"]=="none" && !encryption_feature(target.descriptor.get(),pool["offset"].asUInt64(),bytes,signature),"userdata-encryption-unverified","Preserving fscrypt-enabled or encrypted userdata requires accepted Android key and resize policy evidence");
        }
        Value capability; for(const auto& item:capabilities["filesystems"])if(item["filesystem"]==type)capability=item;
        require(capability["check_available"]==true && (preserve ? row["action"]=="UNCHANGED_GEOMETRY" || capability["offline_resize_available"]==true : capability["format_available"]==true),
            "missing-tool","A selected filesystem formatter, resizer or independent checker is unavailable in this recovery");
    }
    Value plan; plan["schema"]=1; plan["operation"]="partition.apply-layout"; plan["operation_id"]=operation_id(); plan["created_utc"]=utc(); plan["firmware_profile"]=profile;
    plan["target_identity"]=target.identity; plan["gpt"]=metadata; plan["userdata_signature"]=signature;
    plan["userdata_sha256"]=range_hash(target.descriptor.get(),pool["offset"].asUInt64(),bytes); plan["protected"]=Value(Json::arrayValue);
    for(const auto& [offset,size]:protected_ranges(plan)) { Value range; range["offset"]=Json::UInt64(offset); range["bytes"]=Json::UInt64(size); range["sha256"]=range_hash(target.descriptor.get(),offset,size); plan["protected"].append(range); }
    plan["estimated_journal_bytes"]=Json::UInt64(bytes*3+margin); plan["chunk_bytes"]=Json::UInt64(std::max<std::uint64_t>(mib,((bytes+8191)/8192+mib-1)/mib*mib));
    plan["execution_scope"]="FILESYSTEMS_AND_GPT_WITHIN_ORIGINAL_USERDATA"; plan["formats_filesystems"]=true; plan["preserves_userdata_files"]=layout["userdata_policy"]=="preserve";
    plan["before_userdata_data_migration"]=false; plan["complete_partition_job"]=false; plan["live_write_backend_ready"]=false; plan["physical_test_record"]=false; plan["private_record"]=true;
    plan["shared_esp_policy"]="Existing ESP payloads are protected. Set the new ESP allocation to zero to retain an existing shared ESP; boot registration requires a separately reviewed operation.";
    plan["interrupt_scenario"]="FORCED_REBOOT"; plan["recovery_policy"]="Inspect exact before/after bytes; resume or rollback only expected writes; refuse unrelated divergence";
    plan["risk"]=layout["userdata_policy"]=="recreate" ? "ERASE_USERDATA_AND_CREATE_OS_FILESYSTEMS" : "OFFLINE_USERDATA_SHRINK_AND_CREATE_OS_FILESYSTEMS";
    plan["android_userdata_boot_compatibility_verified"]=false;
    plan["warnings"]=Value(Json::arrayValue);
    for(const auto& warning:layout["warnings"])if(!warning.asString().starts_with("GPT metadata execution"))plan["warnings"].append(warning);
    plan["preserved_esps"]=Value(Json::arrayValue);
    for(const auto& part:metadata["current_table"]["partitions"])if(part["type_guid"]=="c12a7328-f81f-11d2-ba4b-00a0c93ec93b") {
        Value preserved=part; preserved["content_policy"]="PRESERVE_EXACT_BYTES";
        preserved["signature"]=filesystem_probe_range(target.descriptor.get(),part["start_lba"].asUInt64()*target.identity["logical_sector_bytes"].asUInt(),part["bytes"].asUInt64());
        plan["preserved_esps"].append(preserved);
    }
    plan["warnings"].append(plan["shared_esp_policy"].asString());
    plan["warnings"].append("This complete image job keeps the original userdata and GPT bytes in a private journal. Staging needs up to three userdata copies plus 64 MiB; keep that journal until acceptance.");
    plan["warnings"].append("Real tablet writes remain blocked. Recreated userdata has no verified Android FBE policy or boot compatibility; an image test is not tablet acceptance.");
    plan["plan_sha256"]=seal(plan,"plan_sha256"); check_plan(plan); storage_revalidate(target,&system); return plan;
}
Value partition_job_execute(const Root& system,StorageTarget& target,const Value& plan,const fs::path& directory,const std::string& confirmation) {
    check_plan(plan); require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the complete partition job hash");
    require(json(target.identity)==json(plan["target_identity"]),"stale-device","Partition job selection changed after review"); storage_revalidate(target,&system); storage_write_gate(target);
    ManagedOperation operation(operation_binding(plan["operation"].asString(),plan,directory,operation_targets(plan["target_identity"])));
    TargetLock claim(target.descriptor.get()); original_verify(target,plan);
    const auto& pool=plan["gpt"]["layout"]["pool"]; const auto signature=filesystem_probe_range(target.descriptor.get(),pool["offset"].asUInt64(),pool["original_bytes"].asUInt64());
    require(json(signature)==json(plan["userdata_signature"]),"stale-source","Userdata filesystem observation changed after review");
    if(plan["gpt"]["layout"]["userdata_policy"]=="preserve")require(!encryption_feature(target.descriptor.get(),pool["offset"].asUInt64(),pool["original_bytes"].asUInt64(),signature),
        "userdata-encryption-unverified","Preserving encrypted userdata requires accepted Android trust and resize policy");
    const auto rebuilt=gpt_layout_plan(target,plan["gpt"]["layout"]["request"],plan["firmware_profile"].asString(),&system);
    for(const auto* key:{"before","after","layout","current_table","desired_table"})require(json(rebuilt[key])==json(plan["gpt"][key]),"stale-plan","Partition policy or GPT differs from the reviewed job");
    auto store=private_directory(directory,true); auto lock=journal_lock(store);
    operation.token().require_binding(operation_binding(plan["operation"].asString(),plan,directory,operation_targets(plan["target_identity"])));
    store.save_record("plan.json",plan);
    Value state; state["schema"]=1; state["plan_sha256"]=plan["plan_sha256"]; state["direction"]="apply"; state["state"]="STAGING"; state["verified"]=false; store.save_record("state.json",state);
    struct statvfs space{};
    require(::fstatvfs(store.fd(),&space)==0 && space.f_frsize && space.f_bavail>plan["estimated_journal_bytes"].asUInt64()/space.f_frsize,
        "insufficient-space","Keep space for original userdata, prepared filesystems and the largest working copy");
    try {
        prepare(system,target,store,directory,plan,state,operation.token()); auto review=inspect(target,store);
        require(json(review.plan)==json(plan),"changed-journal","Partition application inspection selected another plan");
        return apply(target,store,directory,std::move(review),false,operation);
    }
    catch(const Error& error) { state["error_code"]=error.code;
        // Once an application manifest exists, inspect owns the decision about
        // what was written. Do not overwrite its durable active-write intent.
        if(!store.exists("application.json")) { try { phase(store,state,"FAILED_SAFE"); } catch(...) {} } throw; }
}
Value partition_job_recover(const Root& system,StorageTarget& target,const fs::path& path,const std::string& operation,const std::string& confirmation) {
    require(operation=="inspect" || operation=="resume" || operation=="rollback" || operation=="cancel","unsupported-partition-operation","Select inspect, resume, rollback or cancel");
    static_cast<void>(system); auto store=private_directory(path,false);
    if(operation=="inspect") { auto lock=inspection_lock(store); return inspect(target,store).result; }
    const auto plan=record(store,"plan.json"); check_plan(plan);
    require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the exact partition journal hash");
    ManagedOperation owner(operation_binding(plan["operation"].asString(),plan,path,operation_targets(plan["target_identity"])),true);
    TargetLock claim(target.descriptor.get()); auto lock=journal_lock(store); journal_binding(store,path);
    owner.token().require_binding(operation_binding(plan["operation"].asString(),plan,path,operation_targets(plan["target_identity"])));
    require(json(record(store,"plan.json"))==json(plan),"changed-journal","Partition recovery plan changed during ownership admission");
    auto review=inspect(target,store,owner.token().has_retained_intent());
    require(json(review.plan)==json(plan),"changed-journal","Partition recovery inspection selected another plan");
    require(action(review.result,operation),"unsafe-recovery","Readback does not authorize this recovery action; divergent bytes require manual investigation");
    journal_binding(store,path);
    if(operation=="cancel") { auto state=review.state; state["original_unchanged_verified"]=true; state["verified"]=true;
        owner.token().require_binding(operation_binding(plan["operation"].asString(),plan,path,operation_targets(plan["target_identity"])));
        phase(store,state,"CANCELLED_SAFE"); return owner.finish(state,true,true); }
    storage_write_gate(target); return apply(target,store,path,std::move(review),operation=="rollback",owner);
}
} // namespace ure
