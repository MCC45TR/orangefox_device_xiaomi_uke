// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

namespace {
constexpr std::uint64_t capacity=100*1024*1024;
enum class Fault { None, BeforeWrite, PartialWrite, ShortWrites, TargetSync, LastSync, EverySync, InitialRecord,
    WrapperRecord, RawPlan, RawState, BeforePlan, AfterPlan, ReplaceWrapper };
Fault fault=Fault::None;
dev_t target_device=0; ino_t target_inode=0; unsigned target_writes=0,target_syncs=0;
std::uint64_t target_written=0; std::string expected_journal;
void check(bool condition,const std::string& message) { if(!condition)throw std::runtime_error(message); }
template<class F> void reject(F action,const std::string& code) {
    try { action(); } catch(const ure::Error& error) { check(error.code==code,"Expected "+code+", received "+error.code); return; }
    throw std::runtime_error("Expected rejection: "+code);
}
bool target_fd(int fd) { struct stat st{}; return ::fstat(fd,&st)==0 && st.st_dev==target_device && st.st_ino==target_inode; }
void select_fault(const ure::fs::path& path,Fault kind) {
    struct stat st{}; check(::stat(path.c_str(),&st)==0,"Cannot identify fault target");
    target_device=st.st_dev; target_inode=st.st_ino; target_writes=0; target_syncs=0; target_written=0; fault=kind;
}
void fill(const ure::fs::path& path,char byte,std::uint64_t bytes=capacity) {
    ure::Fd fd(::open(path.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600)); check(fd.get()>=0,"Cannot create fixture image");
    std::array<char,65536> data{}; data.fill(byte);
    for(std::uint64_t at=0;at<bytes;) {
        const auto amount=static_cast<std::size_t>(std::min<std::uint64_t>(data.size(),bytes-at));
        check(::pwrite(fd.get(),data.data(),amount,static_cast<off_t>(at))==static_cast<ssize_t>(amount),"Cannot fill fixture image"); at+=amount;
    }
    check(::fsync(fd.get())==0,"Cannot sync fixture image");
}
void all_bytes(const ure::fs::path& path,char expected) {
    ure::Fd fd(::open(path.c_str(),O_RDONLY|O_CLOEXEC)); check(fd.get()>=0,"Cannot open independent byte oracle");
    std::array<char,65536> data{},wanted{}; wanted.fill(expected); std::uint64_t total=0;
    for(;;) { const auto count=::read(fd.get(),data.data(),data.size()); check(count>=0,"Independent read failed"); if(count==0)break;
        check(std::memcmp(data.data(),wanted.data(),static_cast<std::size_t>(count))==0,"Independent byte oracle disagrees"); total+=static_cast<std::uint64_t>(count); }
    check(total==capacity,"Independent capacity oracle disagrees");
}
std::string hash(const ure::fs::path& path) { ure::Fd fd(::open(path.c_str(),O_RDONLY|O_CLOEXEC)); check(fd.get()>=0,"Cannot hash fixture"); return ure::sha256(fd.get()); }
std::string fd_path(int fd) {
    std::array<char,4096> path{}; const auto n=::readlink(("/proc/self/fd/"+std::to_string(fd)).c_str(),path.data(),path.size());
    return n>0 && n<static_cast<ssize_t>(path.size()) ? std::string(path.data(),static_cast<std::size_t>(n)) : std::string();
}
struct Fixture {
    ure::fs::path root,active,fallback,stage,backup,journal;
    ure::Value plan;
    Fixture(const ure::fs::path& base,const std::string& name,const std::string& slot) {
        root=base/name; ure::fs::create_directory(root); ::chmod(root.c_str(),0700);
        active=root/"active.img"; fallback=root/"inactive.img"; stage=root/"stage"; backup=root/"prepared"; journal=root/"journal";
        ure::fs::create_directory(stage); ::chmod(stage.c_str(),0700);
        fill(active,'1'); fill(fallback,'q'); fill(stage/"replacement.img",'h');
        ure::Value request; request["profile"]="fixture-only"; request["active_slot"]=slot; request["inactive_slot"]=slot=="_a" ? "_b" : "_a";
        request["image_sha256"]=hash(stage/"replacement.img"); ure::Root system("/"),staged(stage);
        auto target=ure::storage_image(active,4096),other=ure::storage_image(fallback,4096);
        plan=ure::recovery_install_prepare(system,target,other,staged,"replacement.img",request,backup);
        const auto manifest=ure::json_file(backup/"plan.json");
        check(manifest["content_origin"]=="reviewed-recovery-image","Installer falsely labelled replacement as a filesystem transformation");
        check(plan["physical_device"]==false && plan["fallback_route_rehearsed"]==false,"Fixture promoted physical acceptance");
    }
    ~Fixture() { fault=Fault::None; ure::fs::remove_all(root); }
};
void killed_install(const Fixture& fixture,Fault kind) {
    select_fault(fixture.active,kind); const auto child=::fork(); check(child>=0,"Cannot fork restart fixture");
    if(child==0) {
        try { ure::Root system("/"); auto target=ure::storage_image(fixture.active,4096,true),fallback=ure::storage_image(fixture.fallback,4096);
            ure::recovery_install_execute(system,target,fallback,fixture.plan,fixture.journal,fixture.plan["plan_sha256"].asString()); ::_exit(4);
        } catch(...) { ::_exit(5); }
    }
    fault=Fault::None; int status=0; bool stopped=false;
    for(unsigned tries=0;tries<20000;++tries) { const auto done=::waitpid(child,&status,WNOHANG|WUNTRACED);
        if(done==child) { stopped=WIFSTOPPED(status); break; } ::usleep(1000); }
    ::kill(child,SIGKILL); const auto done=::waitpid(child,&status,0);
    check(stopped && done==child && WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL,"Installer failed to pause at the requested write boundary");
}
} // namespace
extern "C" ssize_t __real_pwrite(int,const void*,size_t,off_t);
extern "C" int __real_fsync(int);
extern "C" ssize_t __real_write(int,const void*,size_t);
extern "C" ssize_t __wrap_pwrite(int fd,const void* bytes,size_t length,off_t offset) {
    if(fault==Fault::None || !target_fd(fd))return __real_pwrite(fd,bytes,length,offset);
    ++target_writes;
    if(fault==Fault::BeforeWrite && target_writes==1) { ::raise(SIGSTOP); return __real_pwrite(fd,bytes,length,offset); }
    if(fault==Fault::PartialWrite && target_writes==1) { const auto result=__real_pwrite(fd,bytes,length,offset); ::raise(SIGSTOP); return result; }
    if(fault==Fault::ShortWrites) { if(target_writes==1) { errno=EINTR; return -1; } length=std::min<size_t>(length,8191); }
    const auto result=__real_pwrite(fd,bytes,length,offset); if(result>0)target_written+=static_cast<std::uint64_t>(result); return result;
}
extern "C" int __wrap_fsync(int fd) {
    if(target_fd(fd)) {
        ++target_syncs;
        if(fault==Fault::TargetSync || (fault==Fault::LastSync && target_written==capacity) || fault==Fault::EverySync) {
            if(fault!=Fault::EverySync)fault=Fault::None;
            errno=EIO; return -1;
        }
    }
    if(fault==Fault::ReplaceWrapper && fd_path(fd)==expected_journal && ure::fs::exists(ure::fs::path(expected_journal)/"installer.json")) {
        fault=Fault::None;
        check(::rename(expected_journal.c_str(),(expected_journal+"-retained").c_str())==0 && ::mkdir(expected_journal.c_str(),0700)==0,"Cannot replace wrapper fixture");
    }
    return __real_fsync(fd);
}
extern "C" ssize_t __wrap_write(int fd,const void* bytes,size_t length) {
    const auto path=fd_path(fd); const auto text=std::string_view(static_cast<const char*>(bytes),length);
    const bool selected=fault==Fault::InitialRecord ||
        (fault==Fault::WrapperRecord && path.find("/journal/.ure-record-")!=std::string::npos) ||
        (fault==Fault::RawPlan && path.find("/journal/raw/.ure-record-")!=std::string::npos && text.find("\"target_identity\"")!=std::string_view::npos) ||
        (fault==Fault::RawState && path.find("/journal/raw/.ure-record-")!=std::string::npos && text.find("\"direction\"")!=std::string_view::npos) ||
        (fault==Fault::BeforePlan && path.find("/journal/raw/before/.ure-record-")!=std::string::npos) ||
        (fault==Fault::AfterPlan && path.find("/journal/raw/after/.ure-record-")!=std::string::npos);
    if(selected) { fault=Fault::None; const auto count=__real_write(fd,bytes,std::min<size_t>(length,7)); ::raise(SIGSTOP); return count; }
    return __real_write(fd,bytes,length);
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    const auto pattern=(ure::fs::path(argv[1])/"installer-fixture-XXXXXX").string(); std::vector<char> temporary(pattern.begin(),pattern.end()); temporary.push_back('\0');
    const auto created=::mkdtemp(temporary.data()); if(!created)return 2; const ure::fs::path work(created);
    try {
        ure::Root system("/");
        // First publication remains absent after a creator dies mid-write.
        const auto records=work/"records"; ure::fs::create_directory(records); ::chmod(records.c_str(),0700);
        const auto child=::fork(); check(child>=0,"Cannot fork record creator");
        if(child==0) { fault=Fault::InitialRecord; ure::Root root(records); ure::Value value; value["proof"]="complete-json-only"; root.save_record("final.json",value); ::_exit(9); }
        int status=0; check(::waitpid(child,&status,WUNTRACED)==child && WIFSTOPPED(status),"Cannot observe initial record interruption");
        ::kill(child,SIGKILL); check(::waitpid(child,&status,0)==child,"Cannot reap record creator");
        check(!ure::fs::exists(records/"final.json"),"Initial record was published partially");
        ure::Root recorded(records); ure::Value record; record["proof"]="complete"; recorded.save_record("final.json",record);
        reject([&] { recorded.save_record("final.json",record); },"io-error"); check(ure::json_file(records/"final.json")==record,"No-replace publication changed the record");
        for(const std::string slot:{"_a","_b"}) {
            Fixture fixture(work,"complete"+slot,slot); auto target=ure::storage_image(fixture.active,4096,true),fallback=ure::storage_image(fixture.fallback,4096);
            const auto confirmation=fixture.plan["plan_sha256"].asString();
            for(const auto& invalid:{ure::Value(Json::arrayValue),ure::Value("invalid"),ure::Value()})
                reject([&] { ure::recovery_install_execute(system,target,fallback,invalid,fixture.journal,confirmation); },"invalid-installer-plan");
            auto malformed=fixture.plan; malformed["fallback_identity"]["bytes"]=ure::Value(Json::objectValue);
            malformed.removeMember("plan_sha256"); malformed["plan_sha256"]=ure::sha256(ure::json(malformed));
            reject([&] { ure::recovery_install_execute(system,target,fallback,malformed,fixture.journal,malformed["plan_sha256"].asString()); },"invalid-installer-plan");
            reject([&] { ure::management_dispatch({"installer","image-execute","fixture-only","--image","missing","--fallback-image","missing","--object","forbidden"}); },"invalid-options");
            reject([&] { ure::recovery_install_execute(system,target,fallback,fixture.plan,fixture.journal,"wrong"); },"confirmation-required");
            check(!ure::fs::exists(fixture.journal),"Refused plan created a journal");
            auto alias=ure::storage_image(fixture.active,4096);
            reject([&] { ure::recovery_install_execute(system,target,alias,fixture.plan,fixture.journal,confirmation); },"installer-fallback-alias");
            const auto ram=ure::fs::path("/tmp")/("installer-journal-"+ure::operation_id());
            reject([&] { ure::recovery_install_execute(system,target,fallback,fixture.plan,ram,confirmation); },"installer-durability-unavailable");
            check(!ure::fs::exists(ram),"RAM-backed refusal published a journal");
            select_fault(fixture.active,Fault::ShortWrites);
            auto state=ure::recovery_install_execute(system,target,fallback,fixture.plan,fixture.journal,confirmation); fault=Fault::None;
            check(state["state"]=="COMMITTED" && state["verified"]==true && state["fallback_unchanged"]==true,"Installer failed full readback");
            all_bytes(fixture.active,'h'); all_bytes(fixture.fallback,'q');
            ure::fs::remove_all(fixture.backup); ure::fs::remove_all(fixture.stage);
            state=ure::recovery_install_recover(system,target,fallback,fixture.journal,"rollback",confirmation);
            check(state["state"]=="ROLLED_BACK" && state["verified"]==true,"Source-independent rollback failed");
            all_bytes(fixture.active,'1'); all_bytes(fixture.fallback,'q');
        }
        for(const auto kind:{Fault::BeforeWrite,Fault::PartialWrite}) {
            Fixture fixture(work,kind==Fault::BeforeWrite ? "before" : "during","_a"); killed_install(fixture,kind);
            auto target=ure::storage_image(fixture.active,4096,true),fallback=ure::storage_image(fixture.fallback,4096);
            const auto inspect=ure::recovery_install_recover(system,target,fallback,fixture.journal,"inspect");
            check(inspect["classification"]==(kind==Fault::BeforeWrite ? "ORIGINAL" : "PARTIAL_EXPECTED_WRITE"),"Restart inspection trusted state instead of bytes");
            ure::fs::remove_all(fixture.backup); ure::fs::remove_all(fixture.stage);
            const auto state=ure::recovery_install_recover(system,target,fallback,fixture.journal,kind==Fault::BeforeWrite ? "cancel" : "resume",fixture.plan["plan_sha256"].asString());
            check(state["state"]==(kind==Fault::BeforeWrite ? "CANCELLED_SAFE" : "COMMITTED"),"Restart action failed");
            all_bytes(fixture.active,kind==Fault::BeforeWrite ? '1' : 'h'); all_bytes(fixture.fallback,'q');
        }
        unsigned boundary=0;
        for(const auto kind:{Fault::WrapperRecord,Fault::RawPlan,Fault::RawState,Fault::BeforePlan,Fault::AfterPlan}) {
            Fixture fixture(work,"record-boundary-"+std::to_string(++boundary),"_b"); killed_install(fixture,kind);
            auto target=ure::storage_image(fixture.active,4096,true),fallback=ure::storage_image(fixture.fallback,4096);
            if(kind==Fault::WrapperRecord) {
                check(!ure::fs::exists(fixture.journal/"installer.json") && !ure::fs::exists(fixture.journal/"raw"),"Partial wrapper admitted raw preparation");
                const auto state=ure::recovery_install_execute(system,target,fallback,fixture.plan,fixture.journal,fixture.plan["plan_sha256"].asString());
                check(state["state"]=="COMMITTED","Unpublished wrapper could not be safely retried");
            } else {
                const auto inspection=ure::recovery_install_recover(system,target,fallback,fixture.journal,"inspect");
                check(inspection["classification"]=="ORIGINAL","Record interruption lost original-byte inspection");
                const auto state=ure::recovery_install_recover(system,target,fallback,fixture.journal,"resume",fixture.plan["plan_sha256"].asString());
                check(state["state"]=="COMMITTED","Interrupted raw/mirror record could not resume");
            }
            all_bytes(fixture.active,'h'); all_bytes(fixture.fallback,'q');
        }
        {
            Fixture fixture(work,"sync-failure","_b"); auto target=ure::storage_image(fixture.active,4096,true),fallback=ure::storage_image(fixture.fallback,4096);
            select_fault(fixture.active,Fault::TargetSync);
            reject([&] { ure::recovery_install_execute(system,target,fallback,fixture.plan,fixture.journal,fixture.plan["plan_sha256"].asString()); },"io-error"); fault=Fault::None;
            check(ure::json_file(fixture.journal/"raw/journal.json")["state"]=="FAILED_UNCERTAIN","Failed sync incorrectly reported safe completion");
            target=ure::storage_image(fixture.active,4096,true);
            const auto state=ure::recovery_install_recover(system,target,fallback,fixture.journal,"rollback",fixture.plan["plan_sha256"].asString());
            check(state["state"]=="ROLLED_BACK","Rollback after failed target sync failed"); all_bytes(fixture.active,'1');
        }
        {
            Fixture fixture(work,"readback-flush","_a"); auto target=ure::storage_image(fixture.active,4096,true),fallback=ure::storage_image(fixture.fallback,4096);
            select_fault(fixture.active,Fault::LastSync);
            reject([&] { ure::recovery_install_execute(system,target,fallback,fixture.plan,fixture.journal,fixture.plan["plan_sha256"].asString()); },"io-error"); fault=Fault::None;
            all_bytes(fixture.active,'h'); target=ure::storage_image(fixture.active,4096,true);
            select_fault(fixture.active,Fault::EverySync);
            reject([&] { ure::recovery_install_recover(system,target,fallback,fixture.journal,"resume",fixture.plan["plan_sha256"].asString()); },"io-error"); fault=Fault::None;
            check(target_writes==0 && target_syncs==1 && ure::json_file(fixture.journal/"raw/journal.json")["state"]=="FAILED_UNCERTAIN",
                "Readback-only resume skipped flush or falsely completed");
            select_fault(fixture.active,Fault::ShortWrites);
            const auto state=ure::recovery_install_recover(system,target,fallback,fixture.journal,"resume",fixture.plan["plan_sha256"].asString()); fault=Fault::None;
            check(state["state"]=="COMMITTED" && target_writes==0 && target_syncs==1,"Readback-only recovery did not retry target synchronization");
        }
        {
            Fixture fixture(work,"foreign-raw","_b"); auto target=ure::storage_image(fixture.active,4096,true),fallback=ure::storage_image(fixture.fallback,4096);
            ure::recovery_install_execute(system,target,fallback,fixture.plan,fixture.journal,fixture.plan["plan_sha256"].asString());
            fill(fixture.stage/"foreign.img",'y'); ure::Root staged(fixture.stage);
            ure::prepared_replacement_backup(system,target,staged,"foreign.img",fixture.root/"foreign-backup","fixture-only",ure::ReplacementOrigin::RecoveryImage);
            const auto foreign=ure::restore_plan(system,target,fixture.root/"foreign-backup","fixture-only");
            ure::restore_execute(system,target,foreign,fixture.root/"foreign-raw",foreign["plan_sha256"].asString());
            ure::fs::rename(fixture.journal/"raw",fixture.journal/"original-raw"); ure::fs::rename(fixture.root/"foreign-raw",fixture.journal/"raw");
            reject([&] { ure::recovery_install_recover(system,target,fallback,fixture.journal,"inspect"); },"wrong-installer-journal");
            all_bytes(fixture.active,'y'); all_bytes(fixture.fallback,'q');
        }
        {
            Fixture fixture(work,"wrapper-replace","_a"); auto target=ure::storage_image(fixture.active,4096,true),fallback=ure::storage_image(fixture.fallback,4096);
            expected_journal=fixture.journal.string(); fault=Fault::ReplaceWrapper;
            reject([&] { ure::recovery_install_execute(system,target,fallback,fixture.plan,fixture.journal,fixture.plan["plan_sha256"].asString()); },"changed-journal"); fault=Fault::None;
            all_bytes(fixture.active,'1'); all_bytes(fixture.fallback,'q');
            check(!ure::fs::exists(fixture.journal/"raw") && !ure::fs::exists(ure::fs::path(expected_journal+"-retained")/"raw"),"Replaced parent admitted a raw writer");
        }
        {
            Fixture fixture(work,"fallback-change","_a"); fill(fixture.fallback,'z');
            auto target=ure::storage_image(fixture.active,4096,true),fallback=ure::storage_image(fixture.fallback,4096);
            reject([&] { ure::recovery_install_execute(system,target,fallback,fixture.plan,fixture.journal,fixture.plan["plan_sha256"].asString()); },"installer-fallback-changed");
            all_bytes(fixture.active,'1'); check(!ure::fs::exists(fixture.journal),"Changed fallback admitted installation");
        }
        ure::fs::remove_all(work);
        std::cout<<"Both fixture slots, persistent mirrors, initial record SIGKILL, pre/partial-write SIGKILL, source-independent recovery, short writes and failed sync passed. Device installation remains unavailable.\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; fault=Fault::None; ure::fs::remove_all(work); return 1; }
}
