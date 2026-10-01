// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <iomanip>
#include <openssl/evp.h>
#include <sstream>
#include <sys/file.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <fcntl.h>
#include <functional>
#include <set>
#include <sys/statvfs.h>

namespace ure {
namespace {
constexpr std::uint64_t max_chunks=16384, min_chunk=65536, max_chunk=64*1024*1024;
class Digest {
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> context_{EVP_MD_CTX_new(),EVP_MD_CTX_free};
public:
    Digest() { require(context_ && EVP_DigestInit_ex(context_.get(),EVP_sha256(),nullptr)==1,"hash-error","Cannot initialize backup digest"); }
    void add(std::string_view data) { require(EVP_DigestUpdate(context_.get(),data.data(),data.size())==1,"hash-error","Cannot update backup digest"); }
    std::string finish() {
        std::array<unsigned char,EVP_MAX_MD_SIZE> bytes{}; unsigned count=0;
        require(EVP_DigestFinal_ex(context_.get(),bytes.data(),&count)==1 && count==32,"hash-error","Cannot finalize backup digest");
        std::ostringstream out; for(unsigned i=0;i<count;++i)out<<std::hex<<std::setw(2)<<std::setfill('0')<<static_cast<unsigned>(bytes[i]); return out.str();
    }
};
Value root_state(const Root& root) {
    const auto st=root.stat("."); Value value; value["device"]=Json::UInt64(st.st_dev); value["inode"]=Json::UInt64(st.st_ino); return value;
}
Value source_state(int fd) {
    struct stat st{};
    require(::fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_size>=0,"invalid-source","Streaming file backups require a sized regular file");
    Value value; value["device"]=Json::UInt64(st.st_dev); value["inode"]=Json::UInt64(st.st_ino);
    value["bytes"]=Json::UInt64(static_cast<std::uint64_t>(st.st_size)); value["uid"]=static_cast<Json::UInt>(st.st_uid);
    value["gid"]=static_cast<Json::UInt>(st.st_gid); value["mode"]=static_cast<Json::UInt>(st.st_mode & 07777);
    value["mtime_seconds"]=Json::Int64(st.st_mtim.tv_sec); value["mtime_nanoseconds"]=Json::Int64(st.st_mtim.tv_nsec);
    value["ctime_seconds"]=Json::Int64(st.st_ctim.tv_sec); value["ctime_nanoseconds"]=Json::Int64(st.st_ctim.tv_nsec);
    return value;
}
std::string plan_seal(Value plan) { plan.removeMember("plan_sha256"); return sha256(json(plan)); }
void check_plan(const Value& plan) {
    const bool file=plan["source_kind"]=="regular-file",storage=plan["source_kind"]=="storage-image" || plan["source_kind"]=="live-block";
    require(((plan["schema"]==1 && file) || (plan["schema"]==2 && storage)) && plan["format"]=="ure-chunked-backup" &&
        plan["firmware_profile"].isString() && identifier(plan["firmware_profile"].asString()) &&
        plan["operation_id"].isString() && identifier(plan["operation_id"].asString()) && plan["root_identity"].isObject() &&
        plan["source_identity"].isObject() && plan["source_identity"]["bytes"].isUInt64() && plan["chunk_bytes"].isUInt64() &&
        plan["chunks"].isArray() && plan["chunks"].size()<=max_chunks && plan["sha256"].isString() && hash_valid(plan["sha256"].asString()) &&
        plan["plan_sha256"].isString() && hash_valid(plan["plan_sha256"].asString()),"invalid-backup","Invalid backup manifest schema");
    require(plan["compression"]=="none" && plan["sparse_representation"]=="expanded-bytes",
        "unsupported-backup","This chunk format requires uncompressed expanded bytes");
    if(file) { require(plan["path"].isString(),"invalid-backup","Backup file path is missing"); components(plan["path"].asString()); }
    else {
        const auto& identity=plan["source_identity"];
        require(identity["logical_sector_bytes"].isUInt() && (identity["logical_sector_bytes"].asUInt()==512 || identity["logical_sector_bytes"].asUInt()==4096) &&
            plan["atomic_snapshot"]==false && plan["restore_authorized"]==false,"invalid-backup","Invalid storage backup boundary");
        if(plan["source_kind"]=="storage-image")require(identity["kind"]=="regular-image" && identity["path"].isString() && fs::path(identity["path"].asString()).is_absolute(),"invalid-backup","Invalid storage image identity");
        else require(identity["kind"]=="live-block" && identity["stable_id"].isString() && identity["unit_identity_available"]==true &&
            identity["unit_identity_sha256"].isString() && hash_valid(identity["unit_identity_sha256"].asString()) &&
            identity["lun_address"].isString() && !identity["lun_address"].asString().empty() &&
            identity["boot_id_sha256"].isString() && hash_valid(identity["boot_id_sha256"].asString()) &&
            identity["boot_id_sha256"].asString()!=sha256(""),"invalid-backup","Live backup lacks unit/LUN/boot identity");
    }
    const auto chunk_bytes=plan["chunk_bytes"].asUInt64(),bytes=plan["source_identity"]["bytes"].asUInt64();
    require(chunk_bytes>=min_chunk && chunk_bytes<=max_chunk && bytes<=INT64_MAX &&
        plan["chunks"].size()==bytes/chunk_bytes+(bytes%chunk_bytes!=0),"invalid-backup","Invalid chunk geometry");
    std::uint64_t offset=0,index=0;
    for(const auto& chunk:plan["chunks"]) {
        const auto size=std::min(chunk_bytes,bytes-offset);
        require(chunk["index"].isUInt64() && chunk["index"].asUInt64()==index && chunk["offset"].isUInt64() &&
            chunk["offset"].asUInt64()==offset && chunk["bytes"].isUInt64() && chunk["bytes"].asUInt64()==size &&
            chunk["sha256"].isString() && hash_valid(chunk["sha256"].asString()),"invalid-backup","Non-contiguous or malformed chunk manifest");
        offset+=size; ++index;
    }
    require(plan["plan_sha256"].asString()==plan_seal(plan),"invalid-backup","Backup manifest checksum differs");
}
Fd source(const Root& root, const Value& plan) {
    check_plan(plan);
    require(json(root_state(root))==json(plan["root_identity"]),"wrong-root","Backup belongs to a different selected root");
    auto file=root.open(plan["path"].asString(),O_RDONLY|O_NONBLOCK);
    require(json(source_state(file.get()))==json(plan["source_identity"]),"stale-source","Backup source identity changed"); return file;
}
void file_unchanged(const Root& root, const Value& plan, int fd) {
    require(json(source_state(fd))==json(plan["source_identity"]),"stale-source","Backup source changed during transfer");
    auto current=source(root,plan); (void)current;
}
class StreamSource {
    const Root& context_;
    Fd file_;
    std::unique_ptr<StorageTarget> target_;
public:
    StreamSource(const Root& context,const Value& plan) : context_(context) {
        check_plan(plan);
        if(plan["source_kind"]=="regular-file")file_=source(context,plan);
        else {
            const auto& identity=plan["source_identity"];
            if(plan["source_kind"]=="live-block")target_=std::make_unique<StorageTarget>(storage_select(context,identity["stable_id"].asString(),true));
            else target_=std::make_unique<StorageTarget>(storage_image(identity["path"].asString(),identity["logical_sector_bytes"].asUInt()));
            require(json(target_->identity)==json(identity),"stale-source","Storage unit, LUN, partition, capacity or boot identity changed");
        }
        check(plan);
    }
    int fd() const { return target_ ? target_->descriptor.get() : file_.get(); }
    bool storage() const { return target_!=nullptr; }
    void destination(const fs::path& path,const Value& plan) const {
        if(plan["source_kind"]!="live-block")return;
        const auto absolute=fs::absolute(path).lexically_normal(); Root parent(absolute.parent_path());
        std::vector<dev_t> devices{parent.stat(".").st_dev};
        if(parent.exists(absolute.filename().string()))devices.push_back(parent.stat(absolute.filename().string()).st_dev);
        const auto usage=storage_usage(context_,target_->identity["stable_id"].asString());
        require(usage["quiescent_observed"]==true,"busy-source","Source ownership became busy before backup destination selection");
        for(const auto device:devices) {
            const auto number=std::to_string(major(device))+":"+std::to_string(minor(device));
            for(const auto& source:usage["related_device_numbers"])require(source!=number,"unsafe-destination","Backup destination is on the source or a related mapper");
        }
    }
    void check(const Value& plan) const {
        if(!target_)file_unchanged(context_,plan,file_.get());
        else {
            storage_revalidate(*target_,&context_);
            if(plan["source_kind"]=="live-block")require(storage_usage(context_,target_->identity["stable_id"].asString())["quiescent_observed"]==true,
                "busy-source","Live source has a mount, writer, swap, USB owner or incomplete ownership observations");
        }
    }
};
void write_all(int fd, std::string_view bytes) {
    while(!bytes.empty()) {
        const auto count=::write(fd,bytes.data(),bytes.size()); if(count<0 && errno==EINTR)continue;
        require(count>0,"io-error","Backup stream write failed"); bytes.remove_prefix(static_cast<std::size_t>(count));
    }
}
// A chunk may be 64 MiB; the working buffer remains 64 KiB.
std::string transfer(int fd, std::uint64_t offset, std::uint64_t bytes, int output, Digest* full=nullptr) {
    Digest digest; std::array<char,65536> buffer{};
    while(bytes>0) {
        const auto amount=static_cast<std::size_t>(std::min(bytes,static_cast<std::uint64_t>(buffer.size())));
        const auto count=::pread(fd,buffer.data(),amount,static_cast<off_t>(offset));
        if(count<0 && errno==EINTR)continue;
        require(count>0,"truncated-source","Backup source ended before the planned boundary");
        const std::string_view data(buffer.data(),static_cast<std::size_t>(count)); digest.add(data); if(full)full->add(data);
        if(output>=0)write_all(output,data);
        bytes-=static_cast<std::uint64_t>(count); offset+=static_cast<std::uint64_t>(count);
    }
    return digest.finish();
}
std::string chunk_name(std::uint64_t index) {
    std::ostringstream name; name<<"chunk-"<<std::setw(5)<<std::setfill('0')<<index<<".bin"; return name.str();
}
Value scan_plan(int fd,Value plan,std::uint64_t chunk_bytes) {
    require(chunk_bytes>=min_chunk && chunk_bytes<=max_chunk,"invalid-chunk-size","Chunk size must be 64 KiB to 64 MiB");
    const auto bytes=plan["source_identity"]["bytes"].asUInt64();
    require(bytes/chunk_bytes+(bytes%chunk_bytes!=0)<=max_chunks,"size-limit","Increase chunk size to stay within the 16384-chunk manifest limit");
    plan["chunk_bytes"]=Json::UInt64(chunk_bytes); plan["chunks"]=Value(Json::arrayValue); plan["compression"]="none";
    plan["format"]="ure-chunked-backup"; plan["operation_id"]=operation_id(); plan["created_utc"]=utc(); plan["tool"]="libuke-recovery/chunk-v2";
    plan["sparse_representation"]="expanded-bytes"; plan["private_record"]=true; plan["physical_test_record"]=false; plan["firmware_identity_validated"]=false;
    plan["restore_requirements"]="Revalidate source/target identities, metadata and a separate restore plan; this manifest does not authorize writes";
    Digest whole; std::uint64_t offset=0,index=0;
    while(offset<bytes) {
        const auto amount=std::min(chunk_bytes,bytes-offset); Value chunk;
        chunk["index"]=Json::UInt64(index++); chunk["offset"]=Json::UInt64(offset); chunk["bytes"]=Json::UInt64(amount);
        chunk["sha256"]=transfer(fd,offset,amount,-1,&whole); plan["chunks"].append(chunk); offset+=amount;
    }
    plan["sha256"]=whole.finish(); plan["plan_sha256"]=plan_seal(plan);
    require(json(plan).size()<=4*1024*1024,"size-limit","Manifest exceeds JSON budget"); check_plan(plan); return plan;
}
void verify_current(StreamSource& source,const Value& plan) {
    source.check(plan);
    if(source.storage()) {
        Digest whole;
        for(const auto& chunk:plan["chunks"])require(transfer(source.fd(),chunk["offset"].asUInt64(),chunk["bytes"].asUInt64(),-1,&whole)==chunk["sha256"].asString(),
            "stale-source","Current storage content differs from the sealed source plan");
        require(whole.finish()==plan["sha256"].asString(),"stale-source","Current storage full hash differs from the source plan");
        source.check(plan);
    }
}
Value store_plan(const Root& store) {
    const auto st=store.stat("plan.json");
    require(S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==::geteuid() && (st.st_mode & 07777)==0600,
        "unsafe-journal","Backup manifest must be a private regular file");
    auto plan=parse_json(store.read("plan.json",4*1024*1024)); check_plan(plan); return plan;
}
Value verify_store(const Root& store, const Value& plan) {
    Digest whole; std::uint64_t completed=0,count=0; bool missing=false;
    for(const auto& chunk:plan["chunks"]) {
        const auto name=chunk_name(chunk["index"].asUInt64());
        if(!store.exists(name)) { missing=true; continue; }
        require(!missing,"invalid-backup","Backup has a gap before a later chunk");
        auto fd=store.open(name,O_RDONLY|O_NONBLOCK); struct stat st{};
        require(::fstat(fd.get(),&st)==0 && S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_size>=0 &&
            st.st_uid==::geteuid() && (st.st_mode & 07777)==0600 && static_cast<std::uint64_t>(st.st_size)==chunk["bytes"].asUInt64(),
            "backup-corrupt","Chunk type, permissions or size differs from manifest");
        require(transfer(fd.get(),0,chunk["bytes"].asUInt64(),-1,&whole)==chunk["sha256"].asString(),"backup-corrupt","Chunk SHA-256 verification failed");
        completed+=chunk["bytes"].asUInt64(); ++count;
    }
    Value result; result["schema"]=1; result["format"]=plan["format"]; result["plan_sha256"]=plan["plan_sha256"];
    result["verified_chunks"]=Json::UInt64(count); result["completed_bytes"]=Json::UInt64(completed);
    result["total_bytes"]=plan["source_identity"]["bytes"]; result["next_chunk"]=Json::UInt64(count);
    result["state"]=missing ? "PARTIAL" : "COMPLETE"; result["private_record"]=true;
    result["source_kind"]=plan["source_kind"]; result["atomic_snapshot"]=false; result["restore_authorized"]=false;
    result["source_current_checked"]=false; result["physical_test_record"]=false;
    result["incomplete_temporary_files"]=0;
    for(const auto& name:store.list(".",static_cast<std::size_t>(max_chunks+1024))) {
        if(name.starts_with(".incomplete-"))result["incomplete_temporary_files"]=result["incomplete_temporary_files"].asUInt()+1;
        if(name.starts_with("chunk-")) {
            require(name.size()==15 && name.substr(11)==".bin" && std::all_of(name.begin()+6,name.begin()+11,[](char c){return c>='0' && c<='9';}),
                "invalid-backup","Unexpected chunk filename");
            const auto index=static_cast<std::uint64_t>(std::stoul(name.substr(6,5)));
            require(index<plan["chunks"].size(),"invalid-backup","Unexpected chunk outside manifest");
        }
    }
    if(!missing) {
        const auto hash=whole.finish(); require(hash==plan["sha256"].asString(),"backup-corrupt","Complete backup SHA-256 differs");
        result["sha256"]=hash; result["verified"]=true;
    } else result["verified"]=false;
    return result;
}
Fd lock_store(const Root& store,bool exclusive=true) {
    auto lock=store.open(".lock",exclusive ? O_RDWR|O_CREAT : O_RDONLY,0600); struct stat st{};
    require(::fstat(lock.get(),&st)==0,"io-error","Cannot inspect backup lock");
    require(S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==::geteuid() && (st.st_mode & 07777)==0600,"unsafe-journal","Backup lock is not private");
    require(::flock(lock.get(),(exclusive ? LOCK_EX : LOCK_SH)|LOCK_NB)==0,"busy-journal","Another process owns this backup"); return lock;
}
}
Value backup_plan(const Root& root, const std::string& relative, const std::string& profile, std::uint64_t chunk_bytes) {
    require(identifier(profile),"invalid-profile","An explicit firmware/profile identifier is required"); components(relative);
    require(chunk_bytes>=min_chunk && chunk_bytes<=max_chunk,"invalid-chunk-size","Chunk size must be 64 KiB to 64 MiB");
    auto fd=root.open(relative,O_RDONLY|O_NONBLOCK); const auto identity=source_state(fd.get());
    Value plan; plan["schema"]=1;
    plan["source_kind"]="regular-file"; plan["path"]=relative; plan["root_identity"]=root_state(root); plan["source_identity"]=identity;
    plan["firmware_profile"]=profile; plan=scan_plan(fd.get(),plan,chunk_bytes); file_unchanged(root,plan,fd.get()); return plan;
}
Value backup_storage_plan(const Root& system,const StorageTarget& target,const std::string& profile,std::uint64_t chunk_bytes) {
    require(identifier(profile),"invalid-profile","An explicit firmware/profile identifier is required"); storage_revalidate(target,&system);
    const bool live=target.identity["kind"]=="live-block";
    require(live || target.identity["kind"]=="regular-image","invalid-source","Select a verified storage image or live block identity");
    if(live) {
        require(target.identity["unit_identity_available"]==true && hash_valid(target.identity["unit_identity_sha256"].asString()) &&
            target.identity["lun_address"].isString() && !target.identity["lun_address"].asString().empty() &&
            hash_valid(target.identity["boot_id_sha256"].asString()) && target.identity["boot_id_sha256"].asString()!=sha256(""),
            "identity-unavailable","Live backup requires verified unit, LUN and current boot identity");
        require(target.exclusive_claim,"claim-required","Live backup planning requires an exclusive read-only kernel claim");
        require(storage_usage(system,target.identity["stable_id"].asString())["quiescent_observed"]==true,"busy-source","Live source ownership is busy or incomplete");
    }
    Value plan; plan["schema"]=2; plan["source_kind"]=live ? "live-block" : "storage-image"; plan["source_identity"]=target.identity;
    plan["root_identity"]["context"]="storage-target"; plan["firmware_profile"]=profile; plan["atomic_snapshot"]=false; plan["restore_authorized"]=false;
    plan["filesystem"]=filesystem_probe(target.descriptor.get()); plan["coherence"]="identity/hash-bound bytes; no atomic filesystem snapshot claim";
    plan=scan_plan(target.descriptor.get(),plan,chunk_bytes); storage_revalidate(target,&system);
    // Reuse the retained kernel claim; a second exclusive open of this same
    // block would correctly fail. Verify content with a second bounded pass.
    Digest whole;
    for(const auto& chunk:plan["chunks"])require(transfer(target.descriptor.get(),chunk["offset"].asUInt64(),chunk["bytes"].asUInt64(),-1,&whole)==chunk["sha256"].asString(),"stale-source","Storage changed during planning");
    require(whole.finish()==plan["sha256"].asString(),"stale-source","Storage full hash changed during planning");
    storage_revalidate(target,&system);
    if(live)require(storage_usage(system,target.identity["stable_id"].asString())["quiescent_observed"]==true,"busy-source","Source ownership changed during planning");
    return plan;
}
Value backup_capture(const Root& root, const Value& plan, const fs::path& directory, bool resume) {
    StreamSource source(root,plan); source.destination(directory,plan);
    auto store=private_directory(directory,!resume); auto lock=lock_store(store);
    if(resume)require(json(store_plan(store))==json(plan),"invalid-backup","Resume manifest differs from the original backup");
    else store.save_record("plan.json",plan);
    auto progress=verify_store(store,plan); progress["state"]="TRANSFERRING"; store.save_record("progress.json",progress,true);
    const auto start=progress["next_chunk"].asUInt64();
    try {
        for(std::uint64_t index=start;index<plan["chunks"].size();++index) {
            source.check(plan); const auto& chunk=plan["chunks"][static_cast<Json::ArrayIndex>(index)];
            const auto temporary=".incomplete-"+operation_id(),destination=chunk_name(index);
            auto output=store.open(temporary,O_RDWR|O_CREAT|O_EXCL,0600);
            try {
                require(transfer(source.fd(),chunk["offset"].asUInt64(),chunk["bytes"].asUInt64(),output.get())==chunk["sha256"].asString(),
                    "stale-source","Source chunk differs from planned SHA-256");
                require(::fsync(output.get())==0,"io-error","Cannot sync backup chunk");
                require(sha256(output.get())==chunk["sha256"].asString(),"backup-corrupt","Chunk readback failed");
                source.check(plan);
                require(::linkat(store.fd(),temporary.c_str(),store.fd(),destination.c_str(),0)==0,"io-error","Cannot publish chunk without overwriting existing data");
                require(::unlinkat(store.fd(),temporary.c_str(),0)==0 && ::fsync(store.fd())==0,"io-error","Cannot sync chunk publication");
            } catch(...) { ::unlinkat(store.fd(),temporary.c_str(),0); throw; }
            progress["verified_chunks"]=Json::UInt64(index+1); progress["next_chunk"]=Json::UInt64(index+1);
            progress["completed_bytes"]=Json::UInt64(chunk["offset"].asUInt64()+chunk["bytes"].asUInt64()); progress["timestamp_utc"]=utc();
            store.save_record("progress.json",progress,true);
        }
        verify_current(source,plan); progress=verify_store(store,plan); progress["source_current_checked"]=true;
        store.save_record("progress.json",progress,true); return progress;
    } catch(const Error& error) {
        progress["state"]="INTERRUPTED"; progress["error_code"]=error.code;
        try { store.save_record("progress.json",progress,true); } catch(...) {} throw;
    }
}
Value backup_verify(const fs::path& directory) {
    auto store=private_directory(directory,false); return verify_store(store,store_plan(store));
}
void backup_export(const Root& root, const Value& plan, std::uint64_t index, int output_fd) {
    StreamSource source(root,plan);
    require(index<plan["chunks"].size(),"invalid-chunk","Chunk index is outside the manifest");
    const auto& chunk=plan["chunks"][static_cast<Json::ArrayIndex>(index)];
    require(transfer(source.fd(),chunk["offset"].asUInt64(),chunk["bytes"].asUInt64(),output_fd)==chunk["sha256"].asString(),
        "stale-source","Transferred chunk differs from planned SHA-256; receiver must discard it");
    source.check(plan);
    if(index+1==plan["chunks"].size())verify_current(source,plan);
}
namespace {
constexpr std::uint64_t restore_margin=32*1024*1024;
bool storage_binding(const Value& current,const Value& expected) {
    if(current["kind"]!=expected["kind"])return false;
    for(const auto* key:{"kind","bytes","logical_sector_bytes"})if(json(current[key])!=json(expected[key]))return false;
    if(expected["kind"]=="regular-image") {
        for(const auto* key:{"path","file_device","file_inode","uid","gid","mode"})if(json(current[key])!=json(expected[key]))return false;
    } else {
        for(const auto* key:{"unit_identity_sha256","lun_address","partition","partition_index","partuuid",
            "start_512_sectors","parent_disk_guid","disk_guid","device_number","stable_id","boot_id_sha256"})
            if(json(current[key])!=json(expected[key]))return false;
    }
    return true;
}
void check_restore_plan(const Value& plan) {
    require(plan["schema"]==1 && plan["operation"]=="storage.restore" &&
        plan["operation_id"].isString() && identifier(plan["operation_id"].asString()) &&
        plan["backup_directory"].isString() && fs::path(plan["backup_directory"].asString()).is_absolute() &&
        plan["backup_root_identity"].isObject() && plan["backup_root_identity"]["device"].isUInt64() &&
        plan["backup_root_identity"]["inode"].isUInt64() && plan["backup_plan_sha256"].isString() && hash_valid(plan["backup_plan_sha256"].asString()) &&
        plan["target_sha256"].isString() && hash_valid(plan["target_sha256"].asString()) &&
        plan["plan_sha256"].isString() && hash_valid(plan["plan_sha256"].asString()) &&
        plan["plan_sha256"].asString()==plan_seal(plan),"invalid-restore-plan","Invalid sealed restore plan");
    check_plan(plan["before"]);
    require(plan["before"]["schema"]==2 && plan["firmware_profile"]==plan["before"]["firmware_profile"] &&
        json(plan["target_identity"])==json(plan["before"]["source_identity"]) && plan["live_write_backend_ready"]==false,
        "invalid-restore-plan","Restore target, profile or write boundary differs from its original-content manifest");
    const auto& identity=plan["target_identity"];
    if(identity["kind"]=="regular-image") {
        require(identity["file_device"].isUInt64() && identity["file_inode"].isUInt64() &&
            identity["uid"].isUInt() && identity["gid"].isUInt() && identity["mode"].isUInt(),
            "invalid-restore-plan","Image restore lacks inode and metadata identity");
    }
    require(identity["bytes"].asUInt64()>0 && identity["bytes"].asUInt64()<=(UINT64_MAX-restore_margin)/2,
        "invalid-restore-plan","Restore size is empty or cannot be represented safely");
    require(plan["estimated_journal_bytes"].isUInt64() &&
        plan["estimated_journal_bytes"].asUInt64()==2*identity["bytes"].asUInt64()+restore_margin && plan["host_streamed_restore"]==false,
        "invalid-restore-plan","Restore space estimate or transfer representation differs from its supported operation");
}
void restore_target(const Root& system,const StorageTarget& target,const Value& expected) {
    if(expected["kind"]=="regular-image") {
        const auto current=storage_image(expected["path"].asString(),expected["logical_sector_bytes"].asUInt());
        struct stat st{};
        require(storage_binding(current.identity,expected) && storage_binding(target.identity,expected) &&
            ::fstat(target.descriptor.get(),&st)==0 && S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_size>=0 &&
            static_cast<std::uint64_t>(st.st_dev)==expected["file_device"].asUInt64() &&
            static_cast<std::uint64_t>(st.st_ino)==expected["file_inode"].asUInt64() &&
            static_cast<std::uint64_t>(st.st_size)==expected["bytes"].asUInt64() &&
            st.st_uid==expected["uid"].asUInt() && st.st_gid==expected["gid"].asUInt() &&
            static_cast<unsigned>(st.st_mode & 07777)==expected["mode"].asUInt(),
            "wrong-target","Restore target path, retained inode, geometry or ownership changed");
    } else {
        storage_revalidate(target,&system);
        require(storage_binding(target.identity,expected),"wrong-target","Restore unit, LUN, partition or boot identity differs");
    }
}
void verified_target(int fd,const Value& manifest,const std::string& code) {
    Digest whole;
    for(const auto& chunk:manifest["chunks"])require(
        transfer(fd,chunk["offset"].asUInt64(),chunk["bytes"].asUInt64(),-1,&whole)==chunk["sha256"].asString(),
        code,"Storage chunk differs from its verified manifest");
    require(whole.finish()==manifest["sha256"].asString(),code,"Storage full hash differs from its verified manifest");
}
Value restore_record(const Root& journal,const std::string& name) {
    const auto st=journal.stat(name);
    require(S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==::geteuid() && (st.st_mode & 07777)==0600,
        "unsafe-journal","Restore record must be an owned private regular file");
    return parse_json(journal.read(name,4*1024*1024));
}
void restore_boundary(const Root& journal,Value& state,const std::string& phase) {
    state["state"]=phase; state["timestamp_utc"]=utc(); journal.save_record("journal.json",state,true);
}
void failed_restore(const Root& journal,Value& state,bool attempted,const std::string& code) {
    state["error_code"]=code;
    state["verified"]=false;
    try { restore_boundary(journal,state,attempted ? "FAILED_UNCERTAIN" : "FAILED_SAFE"); } catch(...) {}
}
class RestoreTargetLock {
    int fd_;
public:
    explicit RestoreTargetLock(int fd) : fd_(fd) {
        require(::flock(fd_,LOCK_EX|LOCK_NB)==0,"busy-target","Another cooperating transaction owns this target");
    }
    ~RestoreTargetLock() { ::flock(fd_,LOCK_UN); }
    RestoreTargetLock(const RestoreTargetLock&)=delete;
    RestoreTargetLock& operator=(const RestoreTargetLock&)=delete;
};
Root mirror_root(const Root& journal,const std::string& name) {
    auto fd=journal.open(name,O_RDONLY|O_DIRECTORY); struct stat st{};
    require(::fstat(fd.get(),&st)==0 && st.st_uid==::geteuid() && (st.st_mode & 07777)==0700,
        "unsafe-journal","Restore mirror must be an owned private directory");
    return Root(std::move(fd));
}
void journal_binding(const fs::path& path,const Root& journal) {
    const auto current=private_directory(path,false);
    require(root_state(current)==root_state(journal),"changed-journal","Restore journal directory was replaced");
}
void desired_manifest(const Value& desired,const Value& plan) {
    check_plan(desired);
    require(desired["schema"]==2 && desired["plan_sha256"]==plan["backup_plan_sha256"] &&
        desired["sha256"]==plan["target_sha256"] && desired["firmware_profile"]==plan["firmware_profile"] &&
        storage_binding(desired["source_identity"],plan["target_identity"]) &&
        json(desired["chunk_bytes"])==json(plan["before"]["chunk_bytes"]),
        "wrong-backup","Restore backup identity, profile, geometry or sealed content differs");
}
bool complete_mirror(const Root& journal,const std::string& name,Value* manifest=nullptr) {
    if(!journal.exists(name))return false;
    const auto store=mirror_root(journal,name);
    if(!store.exists("plan.json")) {
        for(const auto& file:store.list("."))require(file==".lock","invalid-journal","Unidentified files in a restore mirror");
        return false;
    }
    const auto plan=store_plan(store); if(manifest)*manifest=plan;
    return verify_store(store,plan)["verified"]==true;
}
void mirror_chunks(const Root& journal,const std::string& name,const Value& manifest,
                   const std::function<Fd(const Value&)>& input,bool storage_offsets,
                   const std::function<void()>& revalidate) {
    if(!journal.exists(name))require(::mkdirat(journal.fd(),name.c_str(),0700)==0 && ::fsync(journal.fd())==0,
        "io-error","Cannot durably create restore mirror");
    auto store=mirror_root(journal,name); auto lock=lock_store(store);
    if(store.exists("plan.json"))require(json(store_plan(store))==json(manifest),"wrong-backup","Restore mirror manifest differs");
    else {
        for(const auto& file:store.list("."))require(file==".lock","invalid-journal","Unidentified files in a restore mirror");
        store.save_record("plan.json",manifest);
    }
    auto progress=verify_store(store,manifest);
    for(std::uint64_t index=progress["next_chunk"].asUInt64();index<manifest["chunks"].size();++index) {
        revalidate(); const auto& chunk=manifest["chunks"][static_cast<Json::ArrayIndex>(index)]; auto source=input(chunk);
        const auto temporary=".incomplete-"+operation_id(),destination=chunk_name(index);
        auto output=store.open(temporary,O_RDWR|O_CREAT|O_EXCL,0600);
        try {
            require(transfer(source.get(),storage_offsets ? chunk["offset"].asUInt64() : 0,chunk["bytes"].asUInt64(),output.get())==chunk["sha256"].asString(),
                "stale-source","Restore mirror source chunk changed");
            require(::fsync(output.get())==0 && sha256(output.get())==chunk["sha256"].asString(),"backup-corrupt","Restore mirror readback failed");
            revalidate();
            require(::linkat(store.fd(),temporary.c_str(),store.fd(),destination.c_str(),0)==0 &&
                ::unlinkat(store.fd(),temporary.c_str(),0)==0 && ::fsync(store.fd())==0,"io-error","Cannot publish verified restore mirror chunk");
        } catch(...) { ::unlinkat(store.fd(),temporary.c_str(),0); throw; }
    }
    revalidate(); require(verify_store(store,manifest)["verified"]==true,"backup-corrupt","Restore mirror is incomplete");
}
void prepare_restore(const Root& system,const StorageTarget& target,const Value& plan,
                     const Root& journal,const fs::path& path,Value& state) {
    const auto& before=plan["before"]; restore_target(system,target,plan["target_identity"]);
    verified_target(target.descriptor.get(),before,"stale-source");
    const auto bytes=before["source_identity"]["bytes"].asUInt64(); std::uint64_t completed=0;
    for(const auto* name:{"before","after"})if(journal.exists(name)) {
        const auto mirror=mirror_root(journal,name);
        if(mirror.exists("plan.json"))completed+=verify_store(mirror,store_plan(mirror))["completed_bytes"].asUInt64();
    }
    require(completed<=2*bytes,"invalid-journal","Restore mirror sizes exceed planned geometry");
    struct statvfs space{}; const auto needed=2*bytes-completed+restore_margin;
    require(::fstatvfs(journal.fd(),&space)==0 && space.f_frsize>0 && needed/space.f_frsize<space.f_bavail,
        "insufficient-space","Restore journal cannot hold original and target data with safety margin");
    state["direction"]="restore"; restore_boundary(journal,state,"BACKUP_STARTED");
    mirror_chunks(journal,"before",before,[&](const Value&)->Fd {
        Fd fd(::fcntl(target.descriptor.get(),F_DUPFD_CLOEXEC,0)); require(fd.get()>=0,"io-error","Cannot retain original-content source"); return fd;
    },true,[&] { journal_binding(path,journal); storage_revalidate(target,&system); });
    Value desired;
    if(complete_mirror(journal,"after",&desired))desired_manifest(desired,plan);
    else {
        auto source=private_directory(plan["backup_directory"].asString(),false); auto lock=lock_store(source,false);
        require(json(root_state(source))==json(plan["backup_root_identity"]),"wrong-backup","Selected backup directory was replaced");
        desired=store_plan(source); desired_manifest(desired,plan);
        require(verify_store(source,desired)["verified"]==true,"backup-corrupt","Restore source backup is incomplete");
        mirror_chunks(journal,"after",desired,[&](const Value& chunk)->Fd { return source.open(chunk_name(chunk["index"].asUInt64()),O_RDONLY|O_NONBLOCK); },false,[&] {
            journal_binding(path,journal); const auto current=private_directory(plan["backup_directory"].asString(),false);
            require(json(root_state(current))==json(plan["backup_root_identity"]),"wrong-backup","Restore source directory changed during preparation");
        });
    }
    restore_boundary(journal,state,"BACKUP_VERIFIED");
    storage_revalidate(target,&system); verified_target(target.descriptor.get(),before,"stale-source");
    restore_boundary(journal,state,"READY");
}
struct RestoreReview { Value plan,state,result,before,after; std::vector<std::string> current_hashes; bool complete=false; };
RestoreReview review_restore(const Root& system,const StorageTarget& target,const Root& journal) {
    RestoreReview review; review.plan=restore_record(journal,"plan.json"); check_restore_plan(review.plan);
    review.state=restore_record(journal,"journal.json"); const auto& plan=review.plan; const auto& phase=review.state["state"];
    require(review.state["schema"]==1 && review.state["operation"]=="storage.restore" &&
        review.state["operation_id"]==plan["operation_id"] && review.state["plan_sha256"]==plan["plan_sha256"] &&
        (review.state["direction"]=="restore" || review.state["direction"]=="rollback"),"invalid-journal","Restore journal is not bound to its persisted plan");
    const std::set<std::string> phases={"VALIDATED","BACKUP_STARTED","BACKUP_VERIFIED","READY","EXECUTING","VERIFYING",
        "COMMITTED","FAILED_SAFE","FAILED_UNCERTAIN","ROLLBACK_REQUIRED","ROLLED_BACK","CANCELLED_SAFE"};
    require(phase.isString() && phases.count(phase.asString())!=0,"invalid-journal","Unknown restore journal phase");
    restore_target(system,target,plan["target_identity"]);
    const bool before_complete=complete_mirror(journal,"before",&review.before);
    if(!review.before.isNull())require(json(review.before)==json(plan["before"]),"wrong-backup","Original-content mirror differs from the restore plan");
    const bool after_complete=complete_mirror(journal,"after",&review.after);
    if(!review.after.isNull())desired_manifest(review.after,plan);
    review.complete=before_complete && after_complete;
    const bool preparing=phase=="VALIDATED" || phase=="BACKUP_STARTED" || phase=="FAILED_SAFE";
    require(review.complete || preparing || phase=="CANCELLED_SAFE","invalid-journal","Write-phase restore journal lacks complete verified mirrors");
    auto& result=review.result; result["schema"]=1; result["operation"]="storage.restore"; result["operation_id"]=plan["operation_id"];
    result["plan_sha256"]=plan["plan_sha256"]; result["state"]=phase; result["target_identity"]=plan["target_identity"];
    result["backup_verified"]=review.complete; result["chunks"]=Value(Json::arrayValue); result["recovery_actions"]=Value(Json::arrayValue);
    result["private_record"]=true; result["physical_test_record"]=false; result["live_write_backend_ready"]=false;
    result["total_bytes"]=plan["target_identity"]["bytes"]; result["direction"]=review.state["direction"];
    bool original=true,wanted=true,expected=true; std::uint64_t offset=0;
    if(!review.complete) {
        Digest whole; transfer(target.descriptor.get(),0,plan["target_identity"]["bytes"].asUInt64(),-1,&whole);
        result["current_sha256"]=whole.finish(); original=result["current_sha256"]==plan["before"]["sha256"]; wanted=false; expected=original;
    } else {
        auto before=mirror_root(journal,"before"),after=mirror_root(journal,"after"); Digest whole;
        std::uint64_t original_bytes=0,wanted_bytes=0;
        for(Json::ArrayIndex i=0;i<review.before["chunks"].size();++i) {
            const auto& chunk=review.before["chunks"][i]; const auto amount=chunk["bytes"].asUInt64(); Digest current;
            auto old=before.open(chunk_name(i),O_RDONLY),next=after.open(chunk_name(i),O_RDONLY);
            bool chunk_original=true,chunk_wanted=true,chunk_expected=true;
            for(std::uint64_t position=0;position<amount;) {
                const auto size=static_cast<std::size_t>(std::min<std::uint64_t>(65536,amount-position));
                const auto was=storage_read(old.get(),position,size),will=storage_read(next.get(),position,size),now=storage_read(target.descriptor.get(),offset+position,size);
                current.add(now); whole.add(now);
                for(std::size_t j=0;j<size;++j) {
                    chunk_original=chunk_original && now[j]==was[j]; chunk_wanted=chunk_wanted && now[j]==will[j];
                    chunk_expected=chunk_expected && (now[j]==was[j] || now[j]==will[j]);
                }
                position+=size;
            }
            review.current_hashes.push_back(current.finish()); original=original && chunk_original; wanted=wanted && chunk_wanted; expected=expected && chunk_expected;
            if(chunk_original)original_bytes+=amount;
            if(chunk_wanted)wanted_bytes+=amount;
            if(i<128) { Value row; row["index"]=i; row["classification"]=chunk_wanted ? "TARGET" : chunk_original ? "ORIGINAL" : chunk_expected ? "PARTIAL_EXPECTED_WRITE" : "DIVERGED"; result["chunks"].append(row); }
            offset+=amount;
        }
        result["current_sha256"]=whole.finish(); result["verified_original_chunk_bytes"]=Json::UInt64(original_bytes);
        result["verified_target_chunk_bytes"]=Json::UInt64(wanted_bytes); result["chunk_count"]=review.before["chunks"].size(); result["chunks_truncated"]=review.before["chunks"].size()>128;
    }
    result["classification"]=wanted ? "TARGET" : original ? "ORIGINAL" : expected ? "PARTIAL_EXPECTED_WRITE" : "DIVERGED";
    restore_target(system,target,plan["target_identity"]);
    const bool image=plan["target_identity"]["kind"]=="regular-image";
    if(preparing && original) { result["recovery_actions"].append("cancel"); if(image && review.state["direction"]=="restore")result["recovery_actions"].append("resume"); }
    if(original && (phase=="READY" || phase=="BACKUP_VERIFIED"))result["recovery_actions"].append("cancel");
    if(review.complete && image && expected && phase!="CANCELLED_SAFE" && phase!="ROLLED_BACK")result["recovery_actions"].append("rollback");
    if(review.complete && image && expected && review.state["direction"]=="restore" &&
        (phase=="READY" || phase=="BACKUP_VERIFIED" || phase=="EXECUTING" || phase=="VERIFYING" || phase=="FAILED_UNCERTAIN"))result["recovery_actions"].append("resume");
    result["cooperating_locks_only"]=true; result["atomic_snapshot"]=false;
    return review;
}
bool restore_action(const Value& review,const std::string& action) {
    for(const auto& value:review["recovery_actions"])if(value==action)return true;
    return false;
}
void restore_write(int fd,int source,std::uint64_t offset,std::uint64_t bytes) {
    for(std::uint64_t position=0;position<bytes;) {
        const auto data=storage_read(source,position,static_cast<std::size_t>(std::min<std::uint64_t>(65536,bytes-position)));
        std::size_t written=0;
        while(written<data.size()) {
            const auto count=::pwrite(fd,data.data()+written,data.size()-written,static_cast<off_t>(offset+position+written));
            if(count<0 && errno==EINTR)continue;
            require(count>0,"io-error","Restore target write failed"); written+=static_cast<std::size_t>(count);
        }
        position+=data.size();
    }
    require(::fsync(fd)==0,"io-error","Cannot sync restored target chunk");
}
Value run_restore(const Root& system,StorageTarget& target,const Root& journal,const fs::path& path,RestoreReview review,bool rollback) {
    auto state=review.state; state["direction"]=rollback ? "rollback" : "restore"; state["completed_bytes"]=Json::UInt64(0); state["written_chunks"]=Json::UInt64(0);
    state.removeMember("error_code"); state["verified"]=false;
    const auto& manifest=rollback ? review.before : review.after; auto store=mirror_root(journal,rollback ? "before" : "after");
    bool attempted=review.result["classification"]!="ORIGINAL" && review.before["sha256"]!=review.after["sha256"];
    try {
        restore_boundary(journal,state,rollback ? "ROLLBACK_REQUIRED" : "EXECUTING");
        for(Json::ArrayIndex i=0;i<manifest["chunks"].size();++i) {
            journal_binding(path,journal); restore_target(system,target,review.plan["target_identity"]); storage_write_gate(target);
            const auto& chunk=manifest["chunks"][i]; const auto offset=chunk["offset"].asUInt64(),bytes=chunk["bytes"].asUInt64();
            const auto current=transfer(target.descriptor.get(),offset,bytes,-1);
            require(current==review.current_hashes[i],"changed-target","Target chunk changed after journal inspection");
            if(current!=chunk["sha256"].asString()) {
                auto input=store.open(chunk_name(i),O_RDONLY|O_NONBLOCK);
                require(transfer(input.get(),0,bytes,-1)==chunk["sha256"].asString(),"backup-corrupt","Restore mirror changed before writing");
                attempted=true; restore_write(target.descriptor.get(),input.get(),offset,bytes);
                require(transfer(target.descriptor.get(),offset,bytes,-1)==chunk["sha256"].asString(),"verification-error","Restored chunk readback differs");
                state["written_chunks"]=Json::UInt64(state["written_chunks"].asUInt64()+1);
            }
            state["completed_bytes"]=Json::UInt64(offset+bytes);
            restore_boundary(journal,state,rollback ? "ROLLBACK_REQUIRED" : "EXECUTING");
        }
        restore_boundary(journal,state,rollback ? "ROLLBACK_REQUIRED" : "VERIFYING");
        journal_binding(path,journal); restore_target(system,target,review.plan["target_identity"]);
        verified_target(target.descriptor.get(),manifest,"verification-error");
        state["verified"]=true; state["recovered_by_readback"]=state["written_chunks"].asUInt64()==0;
        state["sha256"]=manifest["sha256"]; restore_boundary(journal,state,rollback ? "ROLLED_BACK" : "COMMITTED");
        if(target.identity["kind"]=="regular-image")target.identity=storage_image(target.identity["path"].asString(),target.identity["logical_sector_bytes"].asUInt()).identity;
        return state;
    } catch(const Error& error) { failed_restore(journal,state,attempted,error.code); throw; }
    catch(...) { failed_restore(journal,state,attempted,"unexpected-error"); throw; }
}
}
Value restore_plan(const Root& system,const StorageTarget& target,const fs::path& backup,const std::string& profile) {
    require(identifier(profile),"invalid-profile","An explicit firmware/profile identifier is required");
    auto source=private_directory(backup,false); auto lock=lock_store(source,false); const auto desired=store_plan(source);
    require(desired["schema"]==2,"unsupported-backup","Raw restore requires a storage-image or live-block manifest");
    require(desired["firmware_profile"]==profile,"wrong-profile","Restore backup belongs to a different declared firmware profile");
    require(storage_binding(target.identity,desired["source_identity"]),"wrong-target","Backup belongs to a different storage inode, unit, LUN, partition or geometry");
    if(!target.identity["disk_guid"].isNull())require(target.identity["disk_guid"]==desired["source_identity"]["disk_guid"],"wrong-target","Current known disk GUID differs from the backup");
    require(verify_store(source,desired)["verified"]==true,"backup-corrupt","Raw restore requires a complete verified backup");
    Value plan; plan["schema"]=1; plan["operation"]="storage.restore"; plan["operation_id"]=operation_id(); plan["created_utc"]=utc();
    plan["firmware_profile"]=profile; plan["target_identity"]=target.identity; plan["backup_directory"]=fs::absolute(backup).lexically_normal().string();
    plan["backup_root_identity"]=root_state(source); plan["backup_plan_sha256"]=desired["plan_sha256"]; plan["target_sha256"]=desired["sha256"];
    plan["before"]=backup_storage_plan(system,target,profile,desired["chunk_bytes"].asUInt64());
    const auto bytes=target.identity["bytes"].asUInt64(); require(bytes>0 && bytes<=(UINT64_MAX-restore_margin)/2,"size-limit","Restore journal estimate overflows");
    plan["estimated_journal_bytes"]=Json::UInt64(2*bytes+restore_margin); plan["private_record"]=true; plan["physical_test_record"]=false;
    plan["live_write_backend_ready"]=false; plan["firmware_identity_validated"]=false; plan["confirmation_required"]=true;
    plan["risk"]="Overwrite the complete selected raw object; keep verified original and target data in the local journal before any write";
    plan["cancel_semantics"]="cancel-before-execution; inspect interrupted writes and explicitly resume or roll back only expected bytes";
    plan["host_streamed_restore"]=false; plan["plan_sha256"]=plan_seal(plan);
    require(json(plan).size()<=4*1024*1024,"size-limit","Restore plan exceeds the JSON budget"); check_restore_plan(plan); return plan;
}
Value restore_execute(const Root& system,StorageTarget& target,const Value& plan,const fs::path& directory,const std::string& confirmation) {
    check_restore_plan(plan); require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the exact reviewed restore plan SHA-256");
    storage_write_gate(target); RestoreTargetLock target_lock(target.descriptor.get());
    require(json(target.identity)==json(plan["target_identity"]),"stale-device","Target metadata changed since restore planning");
    storage_revalidate(target,&system); verified_target(target.descriptor.get(),plan["before"],"stale-source");
    auto journal=private_directory(directory,true); auto lock=lock_store(journal); journal.save_record("plan.json",plan);
    Value state; state["schema"]=1; state["operation"]="storage.restore"; state["operation_id"]=plan["operation_id"];
    state["plan_sha256"]=plan["plan_sha256"]; state["direction"]="restore"; state["private_record"]=true; state["physical_test_record"]=false;
    restore_boundary(journal,state,"VALIDATED");
    try { prepare_restore(system,target,plan,journal,directory,state); }
    catch(const Error& error) { failed_restore(journal,state,false,error.code); throw; }
    catch(...) { failed_restore(journal,state,false,"unexpected-error"); throw; }
    return run_restore(system,target,journal,directory,review_restore(system,target,journal),false);
}
Value restore_inspect(const Root& system,const StorageTarget& target,const fs::path& directory) {
    auto journal=private_directory(directory,false); auto lock=lock_store(journal,false); return review_restore(system,target,journal).result;
}
Value restore_resume(const Root& system,StorageTarget& target,const fs::path& directory,const std::string& confirmation) {
    auto journal=private_directory(directory,false); auto lock=lock_store(journal); RestoreTargetLock target_lock(target.descriptor.get());
    auto review=review_restore(system,target,journal);
    require(confirmation==review.plan["plan_sha256"].asString(),"confirmation-required","Confirm the reviewed restore journal SHA-256");
    require(restore_action(review.result,"resume"),"unsafe-resume","Restore cannot resume with diverged bytes or this recorded direction/phase");
    storage_write_gate(target);
    if(!review.complete || review.state["state"]=="VALIDATED" || review.state["state"]=="BACKUP_STARTED" || review.state["state"]=="FAILED_SAFE") {
        try { prepare_restore(system,target,review.plan,journal,directory,review.state); }
        catch(const Error& error) { failed_restore(journal,review.state,false,error.code); throw; }
        catch(...) { failed_restore(journal,review.state,false,"unexpected-error"); throw; }
        review=review_restore(system,target,journal);
    }
    return run_restore(system,target,journal,directory,std::move(review),false);
}
Value restore_rollback(const Root& system,StorageTarget& target,const fs::path& directory,const std::string& confirmation) {
    auto journal=private_directory(directory,false); auto lock=lock_store(journal); RestoreTargetLock target_lock(target.descriptor.get());
    auto review=review_restore(system,target,journal);
    require(confirmation==review.plan["plan_sha256"].asString(),"confirmation-required","Confirm the reviewed restore rollback SHA-256");
    require(review.result["classification"]!="DIVERGED","changed-target","Unrelated target changes prevent rollback");
    require(restore_action(review.result,"rollback"),"unsafe-rollback","Restore lacks complete verified mirrors or an eligible phase");
    storage_write_gate(target); return run_restore(system,target,journal,directory,std::move(review),true);
}
Value restore_cancel(const Root& system,const StorageTarget& target,const fs::path& directory,const std::string& confirmation) {
    auto journal=private_directory(directory,false); auto lock=lock_store(journal); RestoreTargetLock target_lock(target.descriptor.get());
    auto review=review_restore(system,target,journal);
    require(confirmation==review.plan["plan_sha256"].asString(),"confirmation-required","Confirm the reviewed restore cancellation SHA-256");
    require(restore_action(review.result,"cancel"),"unsafe-cancel","Only an original, verified pre-execution target permits safe cancellation");
    review.state["verified"]=true; restore_boundary(journal,review.state,"CANCELLED_SAFE"); return review.state;
}
} // namespace ure
