// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include "operation_guard.hpp"
#include "../install_policy.h"
#include <algorithm>
#include <fcntl.h>
#include <linux/magic.h>
#include <sys/file.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace ure {
namespace {
std::string seal(Value value) { value.removeMember("plan_sha256"); return sha256(json(value)); }
Value directory_identity(const Root& root) {
    const auto st=root.stat("."); Value out; out["device"]=Json::UInt64(st.st_dev); out["inode"]=Json::UInt64(st.st_ino); return out;
}
void persistent(const Root& directory) {
    struct statfs filesystem{}; struct statvfs space{};
    require(::fstatfs(directory.fd(),&filesystem)==0 && ::fstatvfs(directory.fd(),&space)==0,
        "installer-durability-unavailable","Cannot establish journal filesystem semantics");
    const auto type=static_cast<unsigned long>(filesystem.f_type);
    require((type==EXT4_SUPER_MAGIC || type==BTRFS_SUPER_MAGIC || type==F2FS_SUPER_MAGIC || type==XFS_SUPER_MAGIC) &&
        (space.f_flag & ST_RDONLY)==0,"installer-durability-unavailable",
        "Recovery transactions require a writable ext4, Btrfs, F2FS or XFS journal; RAM, overlay and unknown filesystems are refused");
    require(::fsync(directory.fd())==0,"installer-durability-unavailable","Journal directory does not support synchronization");
}
void persistent_parent(const fs::path& destination) {
    Root parent(destination.parent_path().empty() ? fs::path(".") : destination.parent_path()); persistent(parent);
}
void image_pair(const StorageTarget& target,const StorageTarget& fallback) {
    require(target.identity["kind"]=="regular-image" && fallback.identity["kind"]=="regular-image",
        "installer-device-unavailable","This recovery transaction backend accepts private regular-image fixtures only");
    require(target.identity["bytes"].asUInt64()==uke::recovery_bytes && fallback.identity["bytes"].asUInt64()==uke::recovery_bytes &&
        target.identity["logical_sector_bytes"]==fallback.identity["logical_sector_bytes"],
        "installer-geometry","Both recovery fixtures must have the same sector size and exactly 100 MiB capacity");
    require(target.identity["file_device"]!=fallback.identity["file_device"] || target.identity["file_inode"]!=fallback.identity["file_inode"],
        "installer-fallback-alias","Active and inactive recovery fixtures must be independent inodes");
}
void request_check(const Value& request) {
    require(request.isObject() && request.size()==4 && request["profile"].isString() && identifier(request["profile"].asString()) &&
        request["image_sha256"].isString() && hash_valid(request["image_sha256"].asString()) &&
        ((request["active_slot"]=="_a" && request["inactive_slot"]=="_b") ||
         (request["active_slot"]=="_b" && request["inactive_slot"]=="_a")),
        "invalid-installer-request","Select a profile, exact image hash and opposite A/B fixture slots");
}
void plan_check(const Value& plan) {
    require(plan.isObject() && plan["request"].isObject() && plan["fallback_identity"].isObject() &&
        plan["restore"].isObject() && plan["restore"]["target_identity"].isObject(),
        "invalid-installer-plan","Installer plan and nested identities must be objects");
    request_check(plan["request"]);
    require(plan["schema"]==1 && plan["operation"]=="recovery.image-install" &&
        plan["plan_sha256"].isString() && hash_valid(plan["plan_sha256"].asString()) && plan["plan_sha256"].asString()==seal(plan) &&
        plan["evidence_class"]=="regular-image-fixture" && plan["physical_device"]==false && plan["live_block_write"]==false &&
        plan["firmware_identity_validated"]==false && plan["fallback_route_rehearsed"]==false &&
        plan["fallback_identity"]["kind"]=="regular-image" && plan["fallback_identity"]["bytes"].isUInt64() &&
        plan["fallback_identity"]["bytes"].asUInt64()==uke::recovery_bytes &&
        plan["fallback_sha256"].isString() && hash_valid(plan["fallback_sha256"].asString()) &&
        plan["restore"]["operation"]=="storage.restore" && plan["restore"]["target_identity"]["kind"]=="regular-image" &&
        plan["restore"]["target_identity"]["bytes"].isUInt64() && plan["restore"]["target_identity"]["bytes"].asUInt64()==uke::recovery_bytes &&
        plan["restore"]["firmware_profile"]==plan["request"]["profile"] && plan["restore"]["target_sha256"]==plan["request"]["image_sha256"],
        "invalid-installer-plan","Installer plan integrity, image or acceptance boundary differs");
}
void fallback_check(const Root& system,const StorageTarget& target,const StorageTarget& fallback,const Value& plan) {
    image_pair(target,fallback); storage_revalidate(fallback,&system);
    require(json(fallback.identity)==json(plan["fallback_identity"]) && sha256(fallback.descriptor.get())==plan["fallback_sha256"].asString(),
        "installer-fallback-changed","Inactive recovery fixture identity or complete content changed");
    const auto& expected=plan["restore"]["target_identity"];
    // The active file's mtime/ctime legitimately change during interrupted writes.
    for(const auto* key:{"kind","path","file_device","file_inode","bytes","logical_sector_bytes","uid","gid","mode"})
        require(json(target.identity[key])==json(expected[key]),"wrong-target","Installer target was replaced or its geometry/metadata changed");
}
bool unpublished_record(const Root& root,const std::string& name) {
    if(!name.starts_with(".ure-record-") || name.size()!=44 ||
        !std::all_of(name.begin()+12,name.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');}))return false;
    const auto st=root.stat(name); return S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==::geteuid() && (st.st_mode & 07777)==0600;
}
Fd journal_lock(const Root& journal) {
    auto file=journal.open("installer.lock",O_RDWR|O_CREAT,0600); struct stat st{};
    require(::fstat(file.get(),&st)==0 && S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==::geteuid() && (st.st_mode & 07777)==0600,
        "unsafe-journal","Installer lock must be an owned private file");
    require(::flock(file.get(),LOCK_EX|LOCK_NB)==0,"busy-journal","Another process owns the installer journal"); return file;
}
Value recorded_plan(const Root& journal) {
    const auto st=journal.stat("installer.json");
    require(S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==::geteuid() && (st.st_mode & 07777)==0600,
        "unsafe-journal","Installer record must be an owned private file");
    const auto record=parse_json(journal.read("installer.json",4*1024*1024));
    require(record.isObject() && record["schema"]==1 && json(record["journal_identity"])==json(directory_identity(journal)),"changed-journal","Installer journal was replaced");
    plan_check(record["plan"]); return record["plan"];
}
void wrapper_binding(const fs::path& directory,const Root& journal) {
    auto current=private_directory(directory,false);
    require(json(directory_identity(current))==json(directory_identity(journal)),"changed-journal","Installer wrapper pathname was replaced");
}
Value result(Value state,const Value& plan) {
    require(state["plan_sha256"]==plan["restore"]["plan_sha256"],"wrong-installer-journal","Raw journal belongs to a different installer replacement");
    state["installer_plan_sha256"]=plan["plan_sha256"]; state["active_slot"]=plan["request"]["active_slot"];
    state["inactive_slot"]=plan["request"]["inactive_slot"]; state["fallback_unchanged"]=true;
    state["journal_filesystem_synced"]=true; state["physical_durability_verified"]=false;
    state["device_fallback_route_accepted"]=false; state["live_block_write"]=false; return state;
}
} // namespace
Value recovery_install_prepare(const Root& system,const StorageTarget& target,const StorageTarget& fallback,
                              const Root& staged,const std::string& file,const Value& request,const fs::path& backup) {
    request_check(request); image_pair(target,fallback); storage_revalidate(target,&system); storage_revalidate(fallback,&system);
    Value admission=request; admission["target_identity"]=target.identity; admission["fallback_identity"]=fallback.identity;
    Value targets=operation_targets(target.identity); targets.append(fallback.identity);
    ManagedOperation operation(operation_binding("installer.prepare",admission,backup,targets));
    auto input=staged.open(file,O_RDONLY|O_NONBLOCK); const auto st=staged.stat(file);
    require(S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_size==static_cast<off_t>(uke::recovery_bytes) &&
        (static_cast<std::uint64_t>(st.st_dev)!=fallback.identity["file_device"].asUInt64() ||
         static_cast<std::uint64_t>(st.st_ino)!=fallback.identity["file_inode"].asUInt64()),
        "invalid-installer-image","Recovery image must be a separate regular file of exact capacity");
    require(sha256(input.get())==request["image_sha256"].asString(),"installer-image-changed","Selected recovery image hash differs");
    persistent_parent(backup); const auto fallback_sha=sha256(fallback.descriptor.get());
    prepared_replacement_backup(system,target,staged,file,backup,request["profile"].asString(),ReplacementOrigin::RecoveryImage,&operation.token());
    const auto raw=restore_plan(system,target,backup,request["profile"].asString());
    require(raw["target_sha256"]==request["image_sha256"],"installer-image-changed","Prepared image hash differs from the reviewed input");
    Value plan; plan["schema"]=1; plan["operation"]="recovery.image-install"; plan["request"]=request; plan["restore"]=raw;
    plan["fallback_identity"]=fallback.identity; plan["fallback_sha256"]=fallback_sha;
    plan["evidence_class"]="regular-image-fixture"; plan["physical_device"]=false; plan["live_block_write"]=false;
    plan["firmware_identity_validated"]=false; plan["fallback_route_rehearsed"]=false;
    plan["risk"]="Replace one complete recovery image; both verified content states must be locally journaled before writing";
    plan["durability"]="filesystem synchronization requested; no claim of device power-loss persistence";
    plan["plan_sha256"]=seal(plan); plan_check(plan); fallback_check(system,target,fallback,plan); return plan;
}
Value recovery_install_execute(const Root& system,StorageTarget& target,const StorageTarget& fallback,
                              const Value& plan,const fs::path& directory,const std::string& confirmation) {
    plan_check(plan); require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the exact installer plan SHA-256");
    Value targets=operation_targets(plan["restore"]["target_identity"]); targets.append(plan["fallback_identity"]);
    ManagedOperation operation(operation_binding("installer.execute",plan,directory,targets));
    fallback_check(system,target,fallback,plan); storage_write_gate(target); persistent_parent(directory);
    auto journal=private_directory(directory,!fs::exists(directory)); persistent(journal); auto lock=journal_lock(journal);
    // Repeating execution is permitted only when the initial wrapper record
    // never published. There could be no raw write before that record existed.
    for(const auto& name:journal.list("."))require(name=="installer.lock" || unpublished_record(journal,name),
        "existing-journal","Existing installer records require explicit inspection and recovery");
    require(sha256(target.descriptor.get())==plan["restore"]["before"]["sha256"].asString(),"changed-target","Installer original bytes changed before durable intent");
    Value record; record["schema"]=1; record["plan"]=plan; record["journal_identity"]=directory_identity(journal);
    journal.save_record("installer.json",record);
    wrapper_binding(directory,journal);
    const auto state=restore_execute(system,target,plan["restore"],directory/"raw",plan["restore"]["plan_sha256"].asString(),&journal,&operation.token());
    wrapper_binding(directory,journal);
    fallback_check(system,target,fallback,plan); auto out=result(state,plan); journal.save_record("outcome.json",out,true); return operation.finish(out,state["verified"]==true,true);
}
Value recovery_install_recover(const Root& system,StorageTarget& target,const StorageTarget& fallback,
                              const fs::path& directory,const std::string& action,const std::string& confirmation) {
    require(action=="inspect" || action=="resume" || action=="rollback" || action=="cancel","invalid-action","Unknown installer journal action");
    auto journal=private_directory(directory,false); persistent(journal); const auto plan=recorded_plan(journal);
    std::unique_ptr<ManagedOperation> operation;
    if(action!="inspect") { Value targets=operation_targets(plan["restore"]["target_identity"]); targets.append(plan["fallback_identity"]);
        operation=std::make_unique<ManagedOperation>(operation_binding("installer.execute",plan,directory,targets),true); }
    auto lock=journal_lock(journal); require(json(recorded_plan(journal))==json(plan),"changed-journal","Installer plan changed during ownership admission");
    wrapper_binding(directory,journal);
    fallback_check(system,target,fallback,plan);
    require(action=="inspect" ? confirmation.empty() : confirmation==plan["plan_sha256"].asString(),
        "confirmation-required","Confirm the exact recorded installer plan; inspection does not accept confirmation");
    const auto raw=directory/"raw"; const auto hash=plan["restore"]["plan_sha256"].asString(); Value state;
    bool initial=!journal.exists("raw");
    if(!initial) {
        auto raw_root=private_directory(raw,false);
        if(!raw_root.exists("journal.json")) {
            auto raw_lock=raw_root.open(".lock",O_RDWR|O_CREAT,0600);
            const auto lock_state=raw_root.stat(".lock");
            require(S_ISREG(lock_state.st_mode) && lock_state.st_nlink==1 && lock_state.st_uid==::geteuid() && (lock_state.st_mode & 07777)==0600,
                "unsafe-journal","Unstarted raw lock must be a private regular file");
            require(::flock(raw_lock.get(),LOCK_EX|LOCK_NB)==0,"busy-journal","Raw preparation is owned by another process");
            for(const auto& name:raw_root.list("."))require(name==".lock" || name=="plan.json" || unpublished_record(raw_root,name),
                "invalid-journal","Unexpected file in unstarted raw preparation");
            if(raw_root.exists("plan.json"))require(json(parse_json(raw_root.read("plan.json",4*1024*1024)))==json(plan["restore"]),
                "wrong-installer-journal","Unstarted raw plan differs from the wrapper");
            initial=true;
        }
    }
    if(initial) {
        require(sha256(target.descriptor.get())==plan["restore"]["before"]["sha256"].asString(),"changed-target","Unstarted installer target changed");
        state["plan_sha256"]=plan["restore"]["plan_sha256"];
        if(journal.exists("outcome.json")) {
            state=parse_json(journal.read("outcome.json")); require(state.isObject() && state["state"]=="CANCELLED_SAFE" &&
                state["plan_sha256"]==plan["restore"]["plan_sha256"] && state["installer_plan_sha256"]==plan["plan_sha256"],
                "invalid-journal","Unexpected unstarted installer outcome");
            require(action=="inspect","unsafe-resume","A cancelled installer cannot be restarted");
        } else if(action=="inspect") { state["state"]="PREPARED_NO_RAW_JOURNAL"; state["classification"]="ORIGINAL";
            state["recovery_actions"].append("resume"); state["recovery_actions"].append("cancel"); }
        else if(action=="cancel") { state["state"]="CANCELLED_SAFE"; state["verified"]=true; }
        else { require(action=="resume","unsafe-rollback","No raw write was started; select cancellation instead of rollback");
            if(journal.exists("raw")) {
                const auto archive="preparation-"+operation_id();
                require(::syscall(SYS_renameat2,journal.fd(),"raw",journal.fd(),archive.c_str(),RENAME_NOREPLACE)==0,
                    "io-error","Cannot retain interrupted raw initialization");
                require(::fsync(journal.fd())==0,"uncertain-save","Cannot sync retained interrupted preparation");
            }
            wrapper_binding(directory,journal); state=restore_execute(system,target,plan["restore"],raw,hash,&journal,&operation->token()); }
    } else if(action=="inspect")state=restore_inspect(system,target,raw);
    else if(action=="resume")state=restore_resume(system,target,raw,hash,&operation->token());
    else if(action=="rollback")state=restore_rollback(system,target,raw,hash,&operation->token());
    else state=restore_cancel(system,target,raw,hash,&operation->token());
    wrapper_binding(directory,journal); fallback_check(system,target,fallback,plan); auto out=result(state,plan);
    if(action!="inspect")journal.save_record("outcome.json",out,true);
    if(operation)return operation->finish(out,state["verified"]==true,true);
    return out;
}
} // namespace ure
