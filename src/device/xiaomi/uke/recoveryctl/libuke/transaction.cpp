// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <fcntl.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sys/file.h>
#include <sys/statvfs.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace ure {
static Value root_identity(const Root& root) {
    struct stat st{};
    require(::fstat(root.fd(),&st)==0,"io-error","Cannot inspect root identity");
    Value identity; identity["device"]=Json::UInt64(st.st_dev); identity["inode"]=Json::UInt64(st.st_ino); return identity;
}
static Value file_identity(const Root& root, const std::string& relative) {
    auto file=root.open(relative,O_RDONLY|O_NONBLOCK); struct stat st{};
    require(::fstat(file.get(),&st)==0 && S_ISREG(st.st_mode) && st.st_nlink==1,
        "invalid-target","File transactions require a regular file with one hard link");
    require(st.st_size>=0 && st.st_size<=1024*1024,"size-limit","File edit exceeds 1 MiB");
    Value identity; identity["device"]=Json::UInt64(st.st_dev); identity["inode"]=Json::UInt64(st.st_ino);
    identity["bytes"]=Json::Int64(st.st_size); identity["sha256"]=sha256(file.get());
    identity["uid"]=static_cast<Json::UInt>(st.st_uid); identity["gid"]=static_cast<Json::UInt>(st.st_gid);
    identity["mode"]=static_cast<Json::UInt>(st.st_mode & 07777);
    identity["mtime_seconds"]=Json::Int64(st.st_mtim.tv_sec); identity["mtime_nanoseconds"]=Json::Int64(st.st_mtim.tv_nsec);
    identity["ctime_seconds"]=Json::Int64(st.st_ctim.tv_sec); identity["ctime_nanoseconds"]=Json::Int64(st.st_ctim.tv_nsec);
    const auto names_size=::flistxattr(file.get(),nullptr,0);
    require(names_size>=0 || errno==ENOTSUP || errno==EOPNOTSUPP,"metadata-unavailable","Cannot fingerprint file attributes");
    require(names_size<=65536,"size-limit","File attribute names exceed limits");
    Value attributes(Json::arrayValue);
    if(names_size>0) {
        std::string names(static_cast<std::size_t>(names_size),'\0');
        require(::flistxattr(file.get(),names.data(),names.size())==names_size,"stale-source","Attributes changed during discovery");
        std::vector<std::string> sorted;
        for(std::size_t offset=0;offset<names.size();) { const auto end=names.find('\0',offset); require(end!=names.npos && end>offset,"invalid-metadata","Invalid attribute name list"); sorted.push_back(names.substr(offset,end-offset)); offset=end+1; }
        std::sort(sorted.begin(),sorted.end()); std::size_t total=0;
        for(const auto& name:sorted) {
            const auto size=::fgetxattr(file.get(),name.c_str(),nullptr,0);
            require(size>=0 && size<=65536,"metadata-unavailable","Cannot fingerprint a bounded file attribute");
            total+=static_cast<std::size_t>(size); require(total<=262144,"size-limit","File attributes exceed metadata budget");
            std::string bytes(static_cast<std::size_t>(size),'\0');
            require(::fgetxattr(file.get(),name.c_str(),bytes.data(),bytes.size())==size,"stale-source","Attribute changed while fingerprinting");
            Value attribute; attribute["name"]=name; attribute["sha256"]=sha256(bytes); attributes.append(attribute);
        }
    }
    identity["attributes_sha256"]=sha256(json(attributes)); return identity;
}
static std::string seal(Value value) { value.removeMember("plan_sha256"); return sha256(json(value)); }
static void plan_schema(const Value& plan) {
    require(plan.isObject() && plan["schema"].isInt() && plan["schema"].asInt()==1 &&
        plan["operation"].isString() && plan["operation"]=="file.replace" &&
        plan["operation_id"].isString() && identifier(plan["operation_id"].asString()) &&
        plan["path"].isString() && plan["payload"].isString() && plan["firmware_profile"].isString() &&
        plan["plan_sha256"].isString() && hash_valid(plan["plan_sha256"].asString()),
        "invalid-plan","Invalid file transaction schema");
    require(plan["plan_sha256"].asString()==seal(plan),"invalid-plan","Plan checksum does not match its contents");
    components(plan["path"].asString());
    require(plan["source_state"].isObject() && plan["source_state"]["sha256"].isString() &&
        hash_valid(plan["source_state"]["sha256"].asString()) && plan["root_identity"].isObject(),
        "invalid-plan","Plan lacks source identities");
    require(plan["payload_sha256"].isString() && sha256(plan["payload"].asString())==plan["payload_sha256"].asString(),
        "invalid-plan","Payload checksum does not match");
    require(plan["payload"].asString().size()<=1024*1024,"size-limit","File payload exceeds limit");
    require(utf8(plan["payload"].asString()) && identifier(plan["firmware_profile"].asString()),
        "invalid-plan","Invalid text payload or profile");
}
void validate_plan(const Root& root, const Value& plan) {
    plan_schema(plan);
    require(json(root_identity(root))==json(plan["root_identity"]),"stale-plan","Root identity changed since planning");
    require(json(file_identity(root,plan["path"].asString()))==json(plan["source_state"]),"stale-plan","Target identity or contents changed since planning");
}
Value transaction_plan(const Root& root, const std::string& file, const std::string& contents, const std::string& firmware) {
    components(file); require(contents.size()<=1024*1024,"size-limit","Editor content exceeds 1 MiB");
    require(utf8(contents),"binary-file","Text editing requires valid UTF-8 without NUL bytes");
    require(identifier(firmware),"invalid-profile","An explicit firmware/profile identifier is required");
    Value plan; plan["schema"]=1; plan["operation"]="file.replace"; plan["operation_id"]=operation_id();
    plan["created_utc"]=utc(); plan["root_identity"]=root_identity(root); plan["path"]=file;
    plan["source_state"]=file_identity(root,file); plan["payload"]=contents;
    plan["payload_sha256"]=sha256(contents); plan["firmware_profile"]=firmware;
    plan["risk"]="MODIFIES_FILES"; plan["cancel_semantics"]="cancel-at-boundary";
    plan["backup_required"]=true; plan["state"]="CREATED";
    plan["private_record"]=true; plan["hardware_identity_validated"]=false;
    plan["plan_sha256"]=seal(plan); return plan;
}
static void write_bytes(int fd, std::string_view bytes) {
    while(!bytes.empty()) {
        const ssize_t n=::write(fd,bytes.data(),bytes.size());
        if(n<0 && errno==EINTR)continue;
        require(n>0,"io-error","Backup write failed"); bytes.remove_prefix(static_cast<std::size_t>(n));
    }
    require(::fsync(fd)==0,"io-error","Cannot sync backup");
}
Value backup_file(const Root& root, const std::string& relative, const fs::path& destination) {
    const auto before=file_identity(root,relative);
    Root parent(destination.parent_path().empty() ? fs::path(".") : destination.parent_path());
    auto out=parent.open(destination.filename().string(),O_RDWR|O_CREAT|O_EXCL,0600);
    const auto contents=root.read(relative);
    require(sha256(contents)==before["sha256"].asString(),"stale-source","Source changed during backup");
    write_bytes(out.get(),contents);
    require(sha256(out.get())==before["sha256"].asString(),"backup-corrupt","Backup readback failed");
    require(::fsync(parent.fd())==0,"io-error","Cannot sync backup directory");
    Value manifest; manifest["schema"]=1; manifest["root_identity"]=root_identity(root);
    manifest["path"]=relative; manifest["identity"]=before; manifest["created_utc"]=utc();
    manifest["verified"]=true; manifest["private_record"]=true;
    return manifest;
}
static Fd journal_lock(const Root& journal) {
    auto lock=journal.open(".lock",O_RDWR|O_CREAT,0600);
    struct stat st{};
    require(::fstat(lock.get(),&st)==0 && S_ISREG(st.st_mode) && st.st_nlink==1 &&
        st.st_uid==::geteuid() && (st.st_mode & 07777)==0600,"unsafe-journal","Journal lock is not a private regular file");
    require(::flock(lock.get(),LOCK_EX|LOCK_NB)==0,"busy-journal","Another process owns this journal");
    return lock;
}
static Value record(const Root& journal, const std::string& name) {
    const auto st=journal.stat(name);
    require(S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==::geteuid() && (st.st_mode & 07777)==0600,
        "unsafe-journal","Journal record is not a private regular file");
    return parse_json(journal.read(name,4*1024*1024));
}
static void boundary(const Root& journal, Value& state, const std::string& next) {
    state["state"]=next; state["timestamp_utc"]=utc();
    journal.save_record("journal.json",state,true);
}
static bool same_metadata(const Value& current, const Value& before) {
    for(const auto* key:{"mode","uid","gid","attributes_sha256"})if(json(current[key])!=json(before[key]))return false;
    return true;
}
static Value checked_backup(const Root& journal, const Value& state) {
    const auto backup=record(journal,"backup.json");
    const auto st=journal.stat("backup.bin");
    require(S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==::geteuid() && (st.st_mode & 07777)==0600,
        "unsafe-journal","Backup is not a private regular file");
    const auto bytes=journal.read("backup.bin");
    require(backup["path"]==state["path"] && json(backup["root_identity"])==json(state["root_identity"]) &&
        json(backup["identity"])==json(state["source_state"]) && backup["verified"]==true &&
        backup["identity"]["sha256"].isString() && sha256(bytes)==backup["identity"]["sha256"].asString(),
        "backup-corrupt","Rollback backup or source identity is invalid");
    return backup;
}
static Value journal_state(const Root& root, const Root& journal, Value& plan) {
    plan=record(journal,"plan.json"); plan_schema(plan);
    const auto state=record(journal,"journal.json");
    require(state["schema"]==1 && state["operation_id"]==plan["operation_id"] && state["path"]==plan["path"] &&
        state["plan_sha256"]==plan["plan_sha256"] && state["expected_sha256"]==plan["payload_sha256"] &&
        json(state["source_state"])==json(plan["source_state"]) && json(state["root_identity"])==json(plan["root_identity"]),
        "invalid-journal","Journal is not bound to its persisted plan");
    require(json(root_identity(root))==json(state["root_identity"]),"wrong-root","Journal belongs to another selected root");
    static const std::vector<std::string> states{"VALIDATED","BACKUP_STARTED","BACKUP_VERIFIED","READY","EXECUTING",
        "VERIFYING","COMMITTED","FAILED_SAFE","FAILED_UNCERTAIN","ROLLBACK_REQUIRED","ROLLED_BACK","CANCELLED_SAFE"};
    require(state["state"].isString() && std::find(states.begin(),states.end(),state["state"].asString())!=states.end(),
        "invalid-journal","Unknown transaction state");
    return state;
}
static Value execute_file(const Root& root, const Value& plan, const Root& journal, Value state) {
    const auto path=plan["path"].asString();
    bool execution_started=false;
    try {
        checked_backup(journal,state);
        validate_plan(root,plan);
        boundary(journal,state,"READY");
        boundary(journal,state,"EXECUTING"); execution_started=true;
        root.atomic_save(path,plan["payload"].asString(),plan["source_state"]["sha256"].asString());
        boundary(journal,state,"VERIFYING");
        const auto after=file_identity(root,path);
        require(after["sha256"]==plan["payload_sha256"] && same_metadata(after,plan["source_state"]),
            "verification-error","Edited content or metadata failed readback");
        state["result_identity"]=after; state["verified"]=true;
        boundary(journal,state,"COMMITTED"); return state;
    } catch(const Error& error) {
        state["error_code"]=error.code;
        try { boundary(journal,state,execution_started ? "FAILED_UNCERTAIN" : "FAILED_SAFE"); } catch(...) {}
        throw;
    }
}
Value transaction_run(const Root& root, const Value& plan, const fs::path& journal_dir, const std::string& confirmation) {
    validate_plan(root,plan);
    require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the exact plan SHA-256");
    auto target_lock=root.open(plan["path"].asString(),O_RDONLY);
    require(::flock(target_lock.get(),LOCK_EX|LOCK_NB)==0,"busy-target","Another transaction owns this file");
    auto journal=private_directory(journal_dir,true); auto lock=journal_lock(journal);
    struct statvfs space{};
    require(::fstatvfs(journal.fd(),&space)==0,"io-error","Cannot inspect backup space");
    const auto needed=json(plan).size()+plan["payload"].asString().size()+static_cast<std::size_t>(plan["source_state"]["bytes"].asInt64())+65536;
    require(space.f_bavail>0 && space.f_frsize>0 && needed/space.f_frsize<space.f_bavail,"insufficient-space","Journal backup space is insufficient");
    journal.save_record("plan.json",plan);
    Value state; state["schema"]=1; state["operation_id"]=plan["operation_id"]; state["path"]=plan["path"];
    state["root_identity"]=plan["root_identity"]; state["plan_sha256"]=plan["plan_sha256"];
    state["expected_sha256"]=plan["payload_sha256"]; state["source_state"]=plan["source_state"];
    boundary(journal,state,"VALIDATED");
    try {
        boundary(journal,state,"BACKUP_STARTED");
        const auto bytes=root.read(plan["path"].asString());
        require(sha256(bytes)==plan["source_state"]["sha256"].asString(),"stale-source","Source changed during backup");
        auto output=journal.open("backup.bin",O_RDWR|O_CREAT|O_EXCL,0600); write_bytes(output.get(),bytes);
        require(sha256(output.get())==plan["source_state"]["sha256"].asString(),"backup-corrupt","Backup readback failed");
        Value backup; backup["schema"]=1; backup["path"]=plan["path"]; backup["root_identity"]=plan["root_identity"];
        backup["identity"]=plan["source_state"]; backup["verified"]=true; backup["private_record"]=true; backup["created_utc"]=utc();
        journal.save_record("backup.json",backup); boundary(journal,state,"BACKUP_VERIFIED");
    } catch(const Error& error) {
        state["error_code"]=error.code; try { boundary(journal,state,"FAILED_SAFE"); } catch(...) {} throw;
    }
    return execute_file(root,plan,journal,state);
}
static Value inspect_journal(const Root& root, const Root& journal) {
    Value plan;
    const auto state=journal_state(root,journal,plan); const auto current=file_identity(root,state["path"].asString());
    Value result; result["schema"]=1; result["operation_id"]=state["operation_id"]; result["last_confirmed_state"]=state["state"];
    result["plan_sha256"]=state["plan_sha256"]; result["read_only"]=true; result["private_record"]=true;
    result["current_identity"]=current; result["backup_verified"]=false; result["recovery_actions"]=Value(Json::arrayValue);
    if(journal.exists("backup.json") && journal.exists("backup.bin")) { checked_backup(journal,state); result["backup_verified"]=true; }
    const bool original=json(current)==json(state["source_state"]);
    const bool metadata=same_metadata(current,state["source_state"]);
    const bool payload=metadata && current["sha256"]==state["expected_sha256"];
    result["current_state"]=original ? "ORIGINAL_UNCHANGED" : payload ? "PAYLOAD_VERIFIED" :
        metadata && current["sha256"]==state["source_state"]["sha256"] ? "ORIGINAL_CONTENT_VERIFIED" : "DIVERGED";
    const auto phase=state["state"].asString();
    const bool terminal=phase=="COMMITTED" || phase=="ROLLED_BACK" || phase=="CANCELLED_SAFE";
    result["incomplete"]=!terminal;
    if(!terminal && phase!="ROLLBACK_REQUIRED" && result["backup_verified"]==true &&
        (original || (payload && (phase=="EXECUTING" || phase=="VERIFYING" || phase=="FAILED_UNCERTAIN"))))result["recovery_actions"].append("resume");
    if(!terminal && original)result["recovery_actions"].append("cancel");
    if(result["backup_verified"]==true && metadata && (payload || current["sha256"]==state["source_state"]["sha256"]) && phase!="ROLLED_BACK")result["recovery_actions"].append("rollback");
    result["recovery_actions"].append("export-private-report");
    return result;
}
Value transaction_inspect(const Root& root, const fs::path& directory) {
    auto journal=private_directory(directory,false); return inspect_journal(root,journal);
}
static void allowed_action(const Value& inspection, const std::string& action) {
    bool allowed=false; for(const auto& item:inspection["recovery_actions"])allowed=allowed || item==action;
    require(allowed,"unsafe-resume","Current identity, backup or recorded phase does not permit this recovery action");
}
Value transaction_resume(const Root& root, const fs::path& directory, const std::string& confirmation) {
    auto journal=private_directory(directory,false); auto lock=journal_lock(journal); Value plan;
    auto state=journal_state(root,journal,plan);
    require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the persisted plan checksum");
    auto target_lock=root.open(plan["path"].asString(),O_RDONLY);
    require(::flock(target_lock.get(),LOCK_EX|LOCK_NB)==0,"busy-target","Another process owns this file");
    const auto inspection=inspect_journal(root,journal); allowed_action(inspection,"resume");
    if(inspection["current_state"]=="PAYLOAD_VERIFIED") {
        state["result_identity"]=inspection["current_identity"]; state["verified"]=true; state["recovered_by_readback"]=true;
        boundary(journal,state,"COMMITTED"); return state;
    }
    return execute_file(root,plan,journal,state);
}
Value transaction_cancel(const Root& root, const fs::path& directory, const std::string& confirmation) {
    auto journal=private_directory(directory,false); auto lock=journal_lock(journal); Value plan;
    auto state=journal_state(root,journal,plan);
    require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the persisted plan checksum");
    auto target_lock=root.open(plan["path"].asString(),O_RDONLY);
    require(::flock(target_lock.get(),LOCK_EX|LOCK_NB)==0,"busy-target","Another process owns this file");
    allowed_action(inspect_journal(root,journal),"cancel");
    validate_plan(root,plan); boundary(journal,state,"CANCELLED_SAFE"); return state;
}
Value transaction_list(const Root& root, const fs::path& directory) {
    Root parent(directory); Value result(Json::arrayValue);
    for(const auto& name:parent.list(".",256)) {
        Value entry; entry["directory"]=name;
        try {
            auto child=parent.open(name,O_RDONLY|O_DIRECTORY); Root selected(std::move(child));
            if(!selected.exists("journal.json"))continue;
            const auto st=selected.stat(".");
            require(st.st_uid==::geteuid() && (st.st_mode & 07777)==0700,"unsafe-journal","Journal directory is not private");
            entry["inspection"]=inspect_journal(root,selected);
        } catch(const Error& error) { entry["error_code"]=error.code; }
        result.append(entry);
    }
    return result;
}
Value transaction_rollback(const Root& root, const fs::path& directory, const std::string& confirmation) {
    auto journal=private_directory(directory,false); auto lock=journal_lock(journal);
    auto state=record(journal,"journal.json");
    if(journal.exists("plan.json")) { Value plan; state=journal_state(root,journal,plan); }
    require(state["schema"].isInt() && state["schema"].asInt()==1 && state["path"].isString() &&
        state["plan_sha256"].isString() && state["expected_sha256"].isString(),"invalid-journal","Journal schema is incomplete");
    require(confirmation==state["plan_sha256"].asString(),"confirmation-required","Confirm the original plan checksum");
    require(json(root_identity(root))==json(state["root_identity"]),"wrong-root","Rollback root identity differs");
    const auto path=state["path"].asString(); auto target_lock=root.open(path,O_RDONLY);
    require(::flock(target_lock.get(),LOCK_EX|LOCK_NB)==0,"busy-target","Another transaction owns the rollback target");
    const auto backup=checked_backup(journal,state); const auto bytes=journal.read("backup.bin");
    require(backup["identity"].isObject() && backup["identity"]["sha256"].isString() &&
        backup["path"]==state["path"] && json(backup["root_identity"])==json(state["root_identity"]) &&
        sha256(bytes)==backup["identity"]["sha256"].asString() && json(backup["identity"])==json(state["source_state"]),
        "backup-corrupt","Rollback backup or identity is invalid");
    const auto current=file_identity(root,path);
    for(const auto* key:{"mode","uid","gid","attributes_sha256"})require(json(current[key])==json(backup["identity"][key]),"changed-target","File metadata changed after the original transaction");
    require(current["sha256"]==state["expected_sha256"] || current["sha256"]==backup["identity"]["sha256"],
        "changed-target","Current file has unrelated changes; rollback refused");
    boundary(journal,state,"ROLLBACK_REQUIRED");
    root.atomic_save(path,bytes,current["sha256"].asString());
    require(file_identity(root,path)["sha256"]==backup["identity"]["sha256"],"verification-error","Rollback readback failed");
    state["verified"]=true; boundary(journal,state,"ROLLED_BACK"); return state;
}
} // namespace ure
