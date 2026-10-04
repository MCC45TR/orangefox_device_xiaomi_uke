// SPDX-License-Identifier: Apache-2.0
// Exact tree ownership recovery on disposable host directories only.
// Link with --wrap=write --wrap=fsync --wrap=renameat.
#include "uke.h"
#include "operation_lease.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

extern "C" ssize_t __real_write(int,const void*,std::size_t);
extern "C" int __real_fsync(int);
extern "C" int __real_renameat(int,const char*,int,const char*);

namespace fault {
enum Mode { None, BeforeInodeRecord, PartialStage, BeforePublish, AfterPublish, AfterComplete,
    AfterCancelled, FailWrite, FailSync, FailRename, SwapParentOnAdmission, SwapStoreOnAdmission };
volatile std::sig_atomic_t mode=None;
int barrier=-1;
dev_t parent_device=0,store_device=0;
ino_t parent_inode=0,store_inode=0;
std::string stage_name,state_name,destination_name,pending_phase;
ure::fs::path coordinator_path,parent_path,store_path,retained_path;
dev_t coordinator_device=0;
ino_t coordinator_inode=0;
void stop() {
    mode=None;
    if(__real_write(barrier,"R",1)!=1)::_exit(3);
    for(;;)::pause();
}
bool selected(int fd,dev_t device,ino_t inode) {
    struct stat st{}; return ::fstat(fd,&st)==0 && st.st_dev==device && st.st_ino==inode;
}
bool present(int fd,const std::string& name) {
    if(name.empty())return false;
    struct stat st{}; return ::fstatat(fd,name.c_str(),&st,AT_SYMLINK_NOFOLLOW)==0;
}
}
extern "C" ssize_t __wrap_write(int fd,const void* bytes,std::size_t size) {
    if(fault::mode!=fault::None) {
        const std::string_view data(static_cast<const char*>(bytes),size);
        if(data.find("\"operation\" : \"tree.restore\"")!=data.npos) {
            const auto plan=ure::parse_json(data);
            // Coordinator owner records contain the operation inside binding;
            // only the persisted restore plan supplies these artifact names.
            if(plan["operation"]=="tree.restore" && plan["staging_name"].isString() && plan["restore_state_record"].isString()) {
                fault::stage_name=plan["staging_name"].asString(); fault::state_name=plan["restore_state_record"].asString();
            }
        }
        if(data.find("\"tree_descendants_created\"")!=data.npos && data.find("\"restore_plan_sha256\"")!=data.npos) {
            fault::pending_phase=ure::parse_json(data)["state"].asString();
            if(fault::mode==fault::FailWrite && fault::pending_phase=="STAGED") {
                fault::mode=fault::None; errno=EIO; return -1;
            }
        }
    }
    return __real_write(fd,bytes,size);
}
extern "C" int __wrap_renameat(int oldfd,const char* oldname,int newfd,const char* newname) {
    if(fault::mode==fault::FailRename && newname==fault::state_name && fault::pending_phase=="PUBLISHING") {
        fault::mode=fault::None; errno=EIO; return -1;
    }
    return __real_renameat(oldfd,oldname,newfd,newname);
}
extern "C" int __wrap_fsync(int fd) {
    if((fault::mode==fault::SwapParentOnAdmission || fault::mode==fault::SwapStoreOnAdmission) &&
        fault::selected(fd,fault::coordinator_device,fault::coordinator_inode)) {
        const auto mode=fault::mode; fault::mode=fault::None;
        const auto original=mode==fault::SwapParentOnAdmission ? fault::parent_path : fault::store_path;
        ure::fs::rename(original,fault::retained_path);
        if(mode==fault::SwapParentOnAdmission)ure::fs::create_directory(original);
        else { ure::fs::copy(fault::retained_path,original,ure::fs::copy_options::recursive);
            if(::chmod(original.c_str(),0700)!=0)throw std::runtime_error("Cannot keep replaced fixture journal private"); }
    }
    if(fault::mode!=fault::None && fault::selected(fd,fault::parent_device,fault::parent_inode) &&
        fault::present(fd,fault::destination_name)) {
        if(fault::mode==fault::FailSync) { fault::mode=fault::None; errno=EIO; return -1; }
        if(fault::mode==fault::AfterPublish) { const int result=__real_fsync(fd); if(result==0)fault::stop(); return result; }
    }
    const int result=__real_fsync(fd);
    if(result!=0 || fault::mode==fault::None)return result;
    if(fault::mode==fault::BeforeInodeRecord && fault::selected(fd,fault::parent_device,fault::parent_inode) &&
        fault::present(fd,fault::stage_name) && !fault::present(fd,fault::destination_name))fault::stop();
    if(fault::mode==fault::PartialStage) {
        std::array<char,4096> path{}; const std::string descriptor="/proc/self/fd/"+std::to_string(fd);
        const auto count=::readlink(descriptor.c_str(),path.data(),path.size());
        if(count>0 && static_cast<std::size_t>(count)<path.size()) {
            const std::string_view selected(path.data(),static_cast<std::size_t>(count));
            if(!fault::stage_name.empty() && selected.find(fault::stage_name+"/data")!=selected.npos)fault::stop();
        }
    }
    if(fault::selected(fd,fault::store_device,fault::store_inode) && fault::present(fd,fault::state_name)) {
        ure::Root store(ure::Fd(::fcntl(fd,F_DUPFD_CLOEXEC,0))); const auto state=ure::parse_json(store.read(fault::state_name));
        const auto phase=state["state"].asString();
        if((fault::mode==fault::BeforePublish && phase=="PUBLISHING") ||
            (fault::mode==fault::AfterComplete && phase=="COMPLETE") ||
            (fault::mode==fault::AfterCancelled && phase=="CANCELLED_SAFE"))fault::stop();
    }
    return result;
}

namespace {
void check(bool value,const std::string& message) { if(!value)throw std::runtime_error(message); }
template<class F> void reject(F action,const std::string& code) {
    try { action(); } catch(const ure::Error& error) { check(error.code==code,"Unexpected refusal "+error.code+", wanted "+code); return; }
    throw std::runtime_error("Expected refusal "+code);
}
void put(const ure::fs::path& path,const std::string& bytes) {
    std::ofstream out(path,std::ios::binary); out<<bytes; check(out.good(),"Cannot write disposable fixture bytes");
}
std::string bytes(const ure::fs::path& path) { return ure::bounded_read(path); }
struct Workspace {
    ure::fs::path path;
    explicit Workspace(const ure::fs::path& base) {
        auto pattern=(ure::fs::absolute(base)/"tree-recovery-test-XXXXXX").string();
        std::vector<char> name(pattern.begin(),pattern.end()); name.push_back('\0');
        const auto* directory=::mkdtemp(name.data()); check(directory,"Cannot create disposable persistent fixture directory"); path=directory;
    }
    ~Workspace() {
        if(std::uncaught_exceptions()>0) { std::cerr<<"Private tree fixture directory retained: "<<path<<'\n'; return; }
        std::error_code error; ure::fs::remove_all(path,error);
    }
};
struct Records {
    std::string name;
    ure::Value plan,state;
};
struct Fixture {
    ure::fs::path path,source,store,parent,destination;
    std::string backup_hash;
    Fixture(const Workspace& work,const std::string& name):path(work.path/name),source(path/"source"),store(path/"store"),
        parent(path/"target-parent"),destination(parent/"installed") {
        ure::fs::create_directories(source/"nested"); ure::fs::create_directory(parent);
        put(source/"data","archived tree recovery bytes\n"); put(source/"nested"/"detail","independent nested content\n");
        check(::link((source/"data").c_str(),(source/"hard").c_str())==0,"Cannot create fixture hardlink");
        check(::symlink("nested/detail",(source/"alias").c_str())==0,"Cannot create fixture symlink");
        check(::setxattr((source/"data").c_str(),"user.fixture","binary-value",12,0)==0,"Cannot create fixture xattr");
        ure::Root root(source); ure::Fd runtime(::socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0)); struct sockaddr_un address{}; address.sun_family=AF_UNIX;
        const auto socket_path="/proc/self/fd/"+std::to_string(root.fd())+"/runtime";
        check(runtime.get()>=0 && socket_path.size()<sizeof(address.sun_path),"Cannot create bounded fixture runtime socket");
        std::memcpy(address.sun_path,socket_path.c_str(),socket_path.size()+1);
        check(::bind(runtime.get(),reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"Cannot bind omitted runtime socket fixture");
        const auto plan=ure::backup_tree_plan(root,".","host-tree-fixture",store);
        backup_hash=plan["plan_sha256"].asString(); check(ure::backup_tree_capture(root,store,backup_hash)["verified"]==true,"Fixture capture failed");
    }
    Records records() const {
        Records found; ure::Root journal(store);
        for(const auto& name:journal.list(".",256))if(name.starts_with("restore-plan-") && name.ends_with(".json")) {
            check(found.name.empty(),"Fixture contains more than one restore plan"); found.name=name; found.plan=ure::json_file(store/name);
        }
        check(!found.name.empty(),"Interrupted restore plan is missing"); found.state=ure::json_file(store/found.plan["restore_state_record"].asString()); return found;
    }
    ure::Value restore() const { return ure::backup_tree_restore(store,destination,backup_hash); }
    ure::Value recover(const Records& record,const std::string& action,const std::string& confirmation={}) const {
        return ure::backup_tree_recover(store,destination,record.name,action,confirmation.empty() && action!="inspect" ? record.plan["plan_sha256"].asString() : confirmation);
    }
};
void arm(const Fixture& fixture,fault::Mode mode,int barrier=-1,const Records* record=nullptr) {
    struct stat parent{},store{}; check(::stat(fixture.parent.c_str(),&parent)==0 && ::stat(fixture.store.c_str(),&store)==0,"Cannot bind fault fixture directories");
    fault::parent_device=parent.st_dev; fault::parent_inode=parent.st_ino; fault::store_device=store.st_dev; fault::store_inode=store.st_ino;
    fault::stage_name=record ? record->plan["staging_name"].asString() : ""; fault::state_name=record ? record->plan["restore_state_record"].asString() : "";
    fault::destination_name=fixture.destination.filename().string(); fault::pending_phase.clear(); fault::barrier=barrier; fault::mode=mode;
    fault::parent_path=fixture.parent; fault::store_path=fixture.store;
    fault::retained_path=fixture.path/(mode==fault::SwapParentOnAdmission ? "detached-parent" : "detached-store");
    struct stat coordinator{}; check(::stat(fault::coordinator_path.c_str(),&coordinator)==0,"Cannot bind fixture coordinator directory");
    fault::coordinator_device=coordinator.st_dev; fault::coordinator_inode=coordinator.st_ino;
}
class Child {
    pid_t pid_;
public:
    explicit Child(pid_t pid):pid_(pid) { check(pid>0,"Cannot fork interrupted tree fixture"); }
    ~Child() { if(pid_>0) { ::kill(pid_,SIGKILL); while(::waitpid(pid_,nullptr,0)<0 && errno==EINTR) {} } }
    void stop() {
        check(::kill(pid_,SIGKILL)==0,"Cannot kill interrupted tree writer"); int status=0; pid_t waited=0;
        do { waited=::waitpid(pid_,&status,0); } while(waited<0 && errno==EINTR);
        check(waited==pid_ && WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL,"Tree writer did not stop at injected SIGKILL boundary"); pid_=-1;
    }
};
bool offered(const ure::Value& inspection,const std::string& action) {
    for(const auto& candidate:inspection["recovery_actions"])if(candidate==action)return true;
    return false;
}
void owner_remains() {
    const auto status=ure::operation_lease_status(); check(status["retained_owner"]==true && status["active_exclusion"]==false,"Interrupted tree owner was not retained");
}
void owner_released() {
    const auto status=ure::operation_lease_status(); check(status["retained_owner"]==false && status["active_exclusion"]==false,"Verified tree recovery did not release its owner");
}
Records interrupted(const Fixture& fixture,fault::Mode mode,bool inspect_live=false,const Records* cancelling=nullptr) {
    int pipes[2]{}; check(::pipe2(pipes,O_CLOEXEC)==0,"Cannot create tree crash barrier"); ure::Fd input(pipes[0]),output(pipes[1]);
    const auto pid=::fork(); check(pid>=0,"Cannot fork tree recovery writer");
    if(pid==0) {
        try {
            input=ure::Fd(); arm(fixture,mode,output.get(),cancelling);
            if(cancelling)fixture.recover(*cancelling,"cancel-unpublished"); else fixture.restore();
            ::_exit(4);
        } catch(const std::exception& error) { std::cerr<<"Interrupted tree fixture: "<<error.what()<<'\n'; ::_exit(2); }
    }
    Child child(pid); output=ure::Fd(); pollfd ready{input.get(),POLLIN,0};
    check(::poll(&ready,1,5000)>0 && (ready.revents&POLLIN),"Tree writer did not reach its injected crash boundary"); char signal=0;
    check(::read(input.get(),&signal,1)==1 && signal=='R',"Tree crash barrier was malformed");
    const auto record=fixture.records();
    if(inspect_live) {
        const auto plan_before=bytes(fixture.store/record.name),state_before=bytes(fixture.store/record.plan["restore_state_record"].asString());
        const auto inspected=fixture.recover(record,"inspect");
        check(inspected["read_only"]==true && inspected["active_operation"]==true && inspected["recovery_actions"].empty(),"Read-only inspection did not observe the active exact owner");
        check(inspected["owner_plan_sha256"]==record.plan["plan_sha256"],"Live inspection reported the wrong owner hash");
        check(bytes(fixture.store/record.name)==plan_before && bytes(fixture.store/record.plan["restore_state_record"].asString())==state_before,"Inspection changed the tree recovery journal");
        reject([&] { fixture.recover(record,ure::fs::exists(fixture.destination) ? "verify-published" : "cancel-unpublished"); },"operation-busy");
    }
    child.stop(); owner_remains(); return fixture.records();
}
void refused_competing_restore(const Fixture& fixture) {
    const auto other=fixture.parent/"unrelated";
    reject([&] { ure::backup_tree_restore(fixture.store,other,fixture.backup_hash); },"operation-recovery-required");
    check(!ure::fs::exists(other),"Unrelated restore bypassed the retained tree owner");
}
void restore_time(const ure::fs::path& path,const struct stat& prior) {
    const struct timespec times[2]{{0,UTIME_OMIT},prior.st_mtim};
    check(::utimensat(AT_FDCWD,path.c_str(),times,0)==0,"Cannot restore disposable fixture modification time");
}
void uncaptured_stage_case(const Workspace& work) {
    Fixture fixture(work,"uncaptured"); const auto record=interrupted(fixture,fault::BeforeInodeRecord,true);
    check(record.state["state"]=="PREPARED" && record.state["stage_identity"].isNull(),"Inode-less crash was not journaled before intent");
    const auto stage=fixture.parent/record.plan["staging_name"].asString(); const auto initial=ure::descriptor_identity(ure::Root(stage).fd());
    const auto inspected=fixture.recover(record,"inspect");
    check(inspected["current_state"]=="UNPUBLISHED_UNVERIFIED_STAGE" && offered(inspected,"cancel-unpublished") &&
        inspected["owner_matches_restore_plan"]==true,"Uncaptured stage did not expose safe absent-destination cancellation");
    refused_competing_restore(fixture);
    const auto cancelled=fixture.recover(record,"cancel-unpublished");
    check(cancelled["state"]=="CANCELLED_SAFE" && cancelled["staging_artifact_preserved"]==true &&
        cancelled["staging_artifact_identity_verified"]==false && cancelled["target_contents_verified"]==false,"Uncaptured staging was misreported as verified or removed");
    check(!ure::fs::exists(fixture.destination) && ure::fs::is_empty(stage) &&
        ure::json(ure::descriptor_identity(ure::Root(stage).fd()))==ure::json(initial),"Cancellation changed the uncaptured staging artifact"); owner_released();
    reject([&] { fixture.recover(record,"cancel-unpublished"); },"operation-owner-missing");
}
void partial_stage_case(const Workspace& work) {
    Fixture fixture(work,"partial"); const auto record=interrupted(fixture,fault::PartialStage);
    check(record.state["state"]=="STAGED" && record.state["stage_identity"].isObject(),"Partial restore lacks its captured staging inode");
    const auto stage=fixture.parent/record.plan["staging_name"].asString(),file=stage/"data"; const auto content=bytes(file);
    struct stat before{}; check(::stat(file.c_str(),&before)==0,"Cannot inspect partial fixture bytes");
    const auto cancelled=fixture.recover(record,"cancel-unpublished");
    struct stat after{}; check(::stat(file.c_str(),&after)==0 && before.st_ino==after.st_ino && before.st_mode==after.st_mode && bytes(file)==content,
        "Cancellation altered the retained partial tree");
    check(cancelled["staging_artifact_identity_verified"]==true && !ure::fs::exists(fixture.destination),"Captured unpublished cancellation proof differs"); owner_released();
}
void changed_stage_case(const Workspace& work) {
    Fixture fixture(work,"changed-stage"); const auto record=interrupted(fixture,fault::BeforePublish);
    const auto stage=fixture.parent/record.plan["staging_name"].asString(),retained=fixture.parent/"original-stage";
    ure::fs::rename(stage,retained); ure::fs::create_directory(stage); put(stage/"unknown","forensic substitute");
    check(fixture.recover(record,"inspect")["current_state"]=="STAGE_DIVERGED","Substituted staging inode was accepted");
    reject([&] { fixture.recover(record,"cancel-unpublished"); },"changed-tree-restore-stage"); owner_remains();
    check(bytes(stage/"unknown")=="forensic substitute","Refusal changed unknown staging data");
    ure::fs::remove_all(stage); ure::fs::rename(retained,stage);
    check(fixture.recover(record,"cancel-unpublished")["state"]=="CANCELLED_SAFE","Original captured stage could not be cancelled"); owner_released();
}
void published_case(const Workspace& work) {
    Fixture fixture(work,"published"); const auto record=interrupted(fixture,fault::AfterPublish,true);
    check(record.state["state"]=="PUBLISHING" && offered(fixture.recover(record,"inspect"),"verify-published"),"Published crash did not expose exact-inode verification");
    refused_competing_restore(fixture);
    reject([&] { fixture.recover(record,"verify-published","wrong"); },"confirmation-required");
    reject([&] { fixture.recover(record,"cancel-unpublished"); },"published-tree-restore");
    reject([&] { ure::backup_tree_recover(fixture.store,fixture.parent/"wrong",record.name,"verify-published",record.plan["plan_sha256"].asString()); },"wrong-tree-restore-target");
    auto altered=record.plan; altered["extra_identity"]="different-reviewed-plan"; altered.removeMember("plan_sha256"); altered["plan_sha256"]=ure::sha256(ure::json(altered));
    auto altered_state=record.state; altered_state["restore_plan_sha256"]=altered["plan_sha256"];
    altered_state.removeMember("state_sha256"); altered_state["state_sha256"]=ure::sha256(ure::json(altered_state));
    ure::save_json(fixture.store/record.name,altered,true);
    ure::save_json(fixture.store/record.plan["restore_state_record"].asString(),altered_state,true);
    reject([&] { ure::backup_tree_recover(fixture.store,fixture.destination,record.name,"verify-published",altered["plan_sha256"].asString()); },"operation-owner-mismatch");
    ure::save_json(fixture.store/record.name,record.plan,true);
    ure::save_json(fixture.store/record.plan["restore_state_record"].asString(),record.state,true); owner_remains();
    const auto copy=fixture.path/"copied-store";
    ure::fs::copy(fixture.store,copy,ure::fs::copy_options::recursive); check(::chmod(copy.c_str(),0700)==0,"Cannot keep copied fixture journal private");
    reject([&] { ure::backup_tree_recover(copy,fixture.destination,record.name,"verify-published",record.plan["plan_sha256"].asString()); },"wrong-tree-restore-journal"); owner_remains();
    const auto original=fixture.parent/"original-published"; ure::fs::rename(fixture.destination,original);
    ure::fs::copy(original,fixture.destination,ure::fs::copy_options::recursive|ure::fs::copy_options::copy_symlinks);
    check(fixture.recover(record,"inspect")["current_state"]=="DESTINATION_UNRECOGNIZED","Content-only directory lookalike was accepted as owned");
    reject([&] { fixture.recover(record,"verify-published"); },"unrecognized-tree-restore-target"); owner_remains();
    ure::fs::remove_all(fixture.destination); ure::fs::rename(original,fixture.destination);
    struct stat file_before{},root_before{};
    check(::stat((fixture.destination/"data").c_str(),&file_before)==0 && ::stat(fixture.destination.c_str(),&root_before)==0,"Cannot inspect published fixtures");
    const auto content=bytes(fixture.destination/"data"); put(fixture.destination/"data","corrupted installed data");
    reject([&] { fixture.recover(record,"verify-published"); },"restore-verification-error"); owner_remains();
    put(fixture.destination/"data",content); restore_time(fixture.destination/"data",file_before);
    for(const auto& directory:{fixture.destination,fixture.destination/"nested"}) {
        struct stat directory_before{}; check(::stat(directory.c_str(),&directory_before)==0,"Cannot inspect directory namespace fixture");
        put(directory/"unarchived","extra entry with restored directory mtime"); restore_time(directory,directory_before);
        reject([&] { fixture.recover(record,"verify-published"); },"restore-verification-error"); owner_remains();
        check(::unlink((directory/"unarchived").c_str())==0,"Cannot remove disposable namespace substitution"); restore_time(directory,directory_before);
    }
    check(::chmod((fixture.destination/"data").c_str(),0400)==0,"Cannot alter metadata fixture");
    reject([&] { fixture.recover(record,"verify-published"); },"restore-verification-error"); owner_remains();
    check(::chmod((fixture.destination/"data").c_str(),file_before.st_mode&07777)==0,"Cannot restore fixture permissions");
    check(::unlink((fixture.destination/"hard").c_str())==0,"Cannot replace hardlink fixture"); put(fixture.destination/"hard",content);
    check(::chmod((fixture.destination/"hard").c_str(),file_before.st_mode&07777)==0 &&
        ::setxattr((fixture.destination/"hard").c_str(),"user.fixture","binary-value",12,0)==0,"Cannot match substituted hardlink metadata");
    restore_time(fixture.destination/"hard",file_before); restore_time(fixture.destination,root_before);
    reject([&] { fixture.recover(record,"verify-published"); },"restore-verification-error"); owner_remains();
    check(::unlink((fixture.destination/"hard").c_str())==0 && ::link((fixture.destination/"data").c_str(),(fixture.destination/"hard").c_str())==0,"Cannot restore hardlink fixture");
    restore_time(fixture.destination,root_before);
    check(fixture.recover(record,"verify-published")["target_contents_verified"]==true,"Verified published tree could not release exact retained ownership"); owner_released();
    reject([&] { fixture.recover(record,"verify-published"); },"operation-owner-missing");
    check(fixture.recover(record,"inspect")["recovery_actions"].empty(),"Already released tree offered owner recovery");
}
void terminal_crash_cases(const Workspace& work) {
    Fixture complete(work,"terminal-complete"); const auto committed=interrupted(complete,fault::AfterComplete,true);
    check(committed.state["state"]=="COMPLETE" && committed.state["target_contents_verified"]==true,"Successful restore did not save COMPLETE before owner release");
    check(complete.recover(committed,"verify-published")["state"]=="COMPLETE","Terminal published recovery failed"); owner_released();
    Fixture cancelled(work,"terminal-cancelled"); const auto staged=interrupted(cancelled,fault::BeforePublish);
    const auto terminal=interrupted(cancelled,fault::AfterCancelled,true,&staged);
    check(terminal.state["state"]=="CANCELLED_SAFE","Cancellation did not publish its terminal recovery state before owner release");
    check(cancelled.recover(terminal,"cancel-unpublished")["state"]=="CANCELLED_SAFE","Terminal absent-destination recovery failed"); owner_released();
}
void failed_durability_cases(const Workspace& work) {
    for(const auto mode:{fault::FailWrite,fault::FailSync,fault::FailRename}) {
        Fixture fixture(work,"failed-"+std::to_string(mode)); arm(fixture,mode);
        reject([&] { fixture.restore(); },"io-error"); fault::mode=fault::None; owner_remains(); const auto record=fixture.records();
        const auto action=ure::fs::exists(fixture.destination) ? "verify-published" : "cancel-unpublished";
        check(offered(fixture.recover(record,"inspect"),action),"Failed journal or directory durability did not offer safe exact-plan recovery");
        fixture.recover(record,action); owner_released();
    }
}
void replaced_admission_paths(const Workspace& work) {
    for(const auto mode:{fault::SwapParentOnAdmission,fault::SwapStoreOnAdmission}) {
        Fixture fixture(work,"admission-"+std::to_string(mode)); arm(fixture,mode);
        reject([&] { fixture.restore(); },mode==fault::SwapParentOnAdmission ? "wrong-tree-restore-target" : "wrong-tree-restore-journal");
        check(fault::mode==fault::None,"Admission path substitution did not occur"); owner_released();
        check(!ure::fs::exists(fixture.destination) && !ure::fs::exists(fault::retained_path/"installed"),"Restore wrote to a replaced or detached target parent");
        ure::Root original(mode==fault::SwapStoreOnAdmission ? fault::retained_path : fixture.store);
        for(const auto& name:original.list(".",256))check(!name.starts_with("restore-plan-") && !name.starts_with("restore-state-"),
            "Admission replacement reached restore journal intent publication");
    }
}
}
int main(int argc,char** argv) {
    try {
        check(argc<=2,"Pass at most one persistent fixture base directory"); Workspace work(argc==2 ? ure::fs::path(argv[1]) : ure::fs::current_path());
        fault::coordinator_path=::getenv("URE_OPERATION_COORDINATOR") ? ure::fs::path(::getenv("URE_OPERATION_COORDINATOR")) : work.path/"coordinator";
        if(!::getenv("URE_OPERATION_COORDINATOR"))ure::configure_operation_coordinator(fault::coordinator_path);
        uncaptured_stage_case(work); partial_stage_case(work); changed_stage_case(work); published_case(work);
        terminal_crash_cases(work); failed_durability_cases(work); replaced_admission_paths(work);
        Fixture successful(work,"success"); const auto result=successful.restore();
        check(result["verified"]==true && result["operation_owner_released"]==true,"Ordinary restore did not finish with independent owner release");
        check(result["runtime_sockets_omitted"].asUInt()==1 && !ure::fs::exists(successful.destination/"runtime"),"Runtime socket omission does not match verified directory namespaces");
        const auto state=successful.records().state; check(state["state"]=="COMPLETE" && state["cleanup_complete"]==true,"Ordinary restore left no terminal recovery record"); owner_released();
        std::cout<<"Tree retained-owner recovery, SIGKILL boundaries, unpublished forensic staging, exact inode/journal/plan refusals, archived bytes/metadata/hardlinks and write/sync/rename failure recovery passed; disposable host directories only.\n"; return 0;
    } catch(const std::exception& error) { fault::mode=fault::None; std::cerr<<error.what()<<'\n'; return 1; }
}
