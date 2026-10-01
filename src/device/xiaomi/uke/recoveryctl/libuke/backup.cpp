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
    if(file) { require(plan["path"].isString(),"invalid-backup","Backup file path is missing"); components(plan["path"].asString()); }
    else {
        const auto& identity=plan["source_identity"];
        require(identity["logical_sector_bytes"].isUInt() && (identity["logical_sector_bytes"].asUInt()==512 || identity["logical_sector_bytes"].asUInt()==4096) &&
            plan["atomic_snapshot"]==false && plan["restore_authorized"]==false,"invalid-backup","Invalid storage backup boundary");
        if(plan["source_kind"]=="storage-image")require(identity["kind"]=="regular-image" && identity["path"].isString() && fs::path(identity["path"].asString()).is_absolute(),"invalid-backup","Invalid storage image identity");
        else require(identity["kind"]=="live-block" && identity["stable_id"].isString() && identity["unit_identity_available"]==true &&
            hash_valid(identity["unit_identity_sha256"].asString()) && identity["lun_address"].isString() && !identity["lun_address"].asString().empty() &&
            hash_valid(identity["boot_id_sha256"].asString()) && identity["boot_id_sha256"].asString()!=sha256(""),"invalid-backup","Live backup lacks unit/LUN/boot identity");
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
Fd lock_store(const Root& store) {
    auto lock=store.open(".lock",O_RDWR|O_CREAT,0600); const auto st=store.stat(".lock");
    require(S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==::geteuid() && (st.st_mode & 07777)==0600,"unsafe-journal","Backup lock is not private");
    require(::flock(lock.get(),LOCK_EX|LOCK_NB)==0,"busy-journal","Another process owns this backup"); return lock;
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
} // namespace ure
