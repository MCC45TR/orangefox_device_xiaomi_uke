// SPDX-License-Identifier: Apache-2.0
// Cooperative ownership on disposable host files, never physical storage.
#include "operation_lease.hpp"
#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <poll.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace retirement_fault {
dev_t device=0;
ino_t inode=0;
volatile std::sig_atomic_t mode=0;
int barrier=-1;
}
// Every normal call reaches the actual kernel fsync. Faults cover completion
// publication before owner removal, all failed restoration writes, and the
// unlink boundary after the verified completion receipt is already durable.
extern "C" int fsync(int descriptor) {
    if(retirement_fault::mode==4) { errno=EIO; return -1; }
    if(retirement_fault::mode!=0) {
        struct stat selected{},owner{},pending{};
        if(::fstat(descriptor,&selected)==0 && S_ISDIR(selected.st_mode) && selected.st_dev==retirement_fault::device && selected.st_ino==retirement_fault::inode &&
            ::fstatat(descriptor,"release.json",&pending,AT_SYMLINK_NOFOLLOW)==0) {
            const auto mode=retirement_fault::mode;
            const bool owner_present=::fstatat(descriptor,"owner.json",&owner,AT_SYMLINK_NOFOLLOW)==0;
            bool completion_visible=false;
            if(owner_present && (mode==3 || mode==5)) {
                const auto read_record=[&](const char* name) {
                    const int record=::openat(descriptor,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC); ure::Value value;
                    if(record>=0) {
                        std::array<char,16384> bytes{}; const auto count=::read(record,bytes.data(),bytes.size()); ::close(record);
                        if(count>0)value=ure::parse_json(std::string_view(bytes.data(),static_cast<std::size_t>(count)));
                    }
                    return value;
                };
                const auto release=read_record("release.json"),retained=read_record("owner.json");
                completion_visible=release["phase"]=="RELEASED" && release["nonce"]==retained["nonce"];
            }
            if((!owner_present && (mode==1 || mode==2)) || (owner_present && completion_visible)) {
                retirement_fault::mode=mode==3 ? 4 : 0;
                if(mode==1 || mode==3) { errno=EIO; return -1; }
                if(mode==2 || mode==5) {
                    if(::write(retirement_fault::barrier,"T",1)!=1)::_exit(3);
                    for(;;)::pause();
                }
            }
        }
    }
    return static_cast<int>(::syscall(SYS_fsync,descriptor));
}

namespace {
void check(bool value,const std::string& message) { if(!value)throw std::runtime_error(message); }
template<class F> void reject(F function,const std::string& expected) {
    try { function(); } catch(const ure::Error& error) {
        check(error.code==expected,"Unexpected refusal: "+error.code+", expected "+expected); return;
    }
    throw std::runtime_error("Expected refusal: "+expected);
}
void child_check(const std::function<void()>& test) {
    const auto pid=::fork(); check(pid>=0,"Cannot fork cooperative ownership fixture");
    if(pid==0) {
        try { test(); ::_exit(0); }
        catch(const std::exception& error) { std::cerr<<"Child fixture: "<<error.what()<<'\n'; ::_exit(1); }
    }
    int status=0; pid_t waited=0;
    do { waited=::waitpid(pid,&status,0); } while(waited<0 && errno==EINTR);
    check(waited==pid && WIFEXITED(status) && WEXITSTATUS(status)==0,"Independent ownership process failed");
}
void file_bytes(const ure::fs::path& path,std::string_view bytes) {
    ure::Fd fd(::open(path.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600));
    check(fd.get()>=0,"Cannot create fixture bytes"); std::size_t done=0;
    while(done<bytes.size()) {
        const auto written=::write(fd.get(),bytes.data()+done,bytes.size()-done);
        if(written<0 && errno==EINTR)continue;
        check(written>0,"Cannot write fixture bytes"); done+=static_cast<std::size_t>(written);
    }
    check(::fsync(fd.get())==0,"Cannot sync fixture bytes");
}
std::string file_hash(const ure::fs::path& path) {
    ure::Fd fd(::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC)); check(fd.get()>=0,"Cannot open byte oracle"); return ure::sha256(fd.get());
}
ure::Value object_identity(const ure::fs::path& path) {
    struct stat st{}; check(::lstat(path.c_str(),&st)==0,"Cannot inspect fixture identity");
    ure::Value value; value["path"]=path.string(); value["file_device"]=Json::UInt64(st.st_dev);
    value["file_inode"]=Json::UInt64(st.st_ino); value["kind"]=S_ISDIR(st.st_mode) ? "directory" : "regular-image";
    return value;
}
ure::OperationBinding binding_for(const ure::fs::path& image,const ure::fs::path& journal) {
    ure::Value targets(Json::arrayValue); targets.append(object_identity(image));
    return {"fixture.storage", "fixture-operation", ure::sha256("exact-reviewed-fixture-plan"),targets,object_identity(journal)};
}
ure::Value verified(const std::string& state="COMPLETE") {
    ure::Value value; value["state"]=state; value["verified"]=true; value["cleanup_complete"]=true;
    value["physical_test_record"]=false; return value;
}
class Child {
    pid_t pid_;
public:
    explicit Child(pid_t pid):pid_(pid) { check(pid_>0,"Cannot create interrupted writer fixture"); }
    ~Child() { if(pid_>0) { static_cast<void>(::kill(pid_,SIGKILL)); while(::waitpid(pid_,nullptr,0)<0 && errno==EINTR) {} } }
    void kill() {
        check(::kill(pid_,SIGKILL)==0,"Cannot interrupt owned fixture process"); int status=0; pid_t waited=0;
        do { waited=::waitpid(pid_,&status,0); } while(waited<0 && errno==EINTR);
        check(waited==pid_ && WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL,"Fixture did not stop at SIGKILL boundary"); pid_=-1;
    }
};
void interrupted_writer(const ure::OperationBinding& binding,const ure::fs::path& image) {
    int pipes[2]{}; check(::pipe2(pipes,O_CLOEXEC)==0,"Cannot create writer barrier"); ure::Fd input(pipes[0]),output(pipes[1]);
    const auto pid=::fork(); check(pid>=0,"Cannot fork interrupted writer");
    if(pid==0) {
        try {
            input=ure::Fd(); auto lease=ure::OperationLease::acquire(binding); lease.checkpoint("EXECUTING");
            ure::Fd target(::open(image.c_str(),O_WRONLY|O_NOFOLLOW|O_CLOEXEC));
            check(target.get()>=0,"Cannot open owned fixture target"); const std::string half(32768,'N');
            check(::pwrite(target.get(),half.data(),half.size(),0)==static_cast<ssize_t>(half.size()) && ::fsync(target.get())==0,"Cannot write/sync fixture frontier");
            check(::write(output.get(),"R",1)==1,"Cannot announce durable fixture frontier");
            for(;;)::pause();
        } catch(const std::exception& error) { std::cerr<<"Interrupted fixture: "<<error.what()<<'\n'; ::_exit(1); }
    }
    Child child(pid); output=ure::Fd(); pollfd ready{input.get(),POLLIN,0};
    check(::poll(&ready,1,5000)>0 && (ready.revents&POLLIN),"Writer did not reach the durable barrier"); char signal=0;
    check(::read(input.get(),&signal,1)==1 && signal=='R',"Writer barrier was malformed");
    const auto active=ure::operation_lease_status();
    check(active["available"]==true && active["active_exclusion"]==true && active["retained_owner"]==true,"Running child did not retain ownership");
    auto different=binding; different.journal["other_journal"]=true;
    reject([&]{ure::OperationLease::acquire(different);},"operation-busy");
    child.kill();
}
void interrupted_retirement(const ure::OperationBinding& binding,const ure::fs::path& domain,int mode) {
    int pipes[2]{}; check(::pipe2(pipes,O_CLOEXEC)==0,"Cannot create retirement barrier"); ure::Fd input(pipes[0]),output(pipes[1]);
    const auto pid=::fork(); check(pid>=0,"Cannot fork interrupted owner retirement");
    if(pid==0) {
        try {
            input=ure::Fd(); auto lease=ure::OperationLease::acquire(binding); lease.checkpoint("RETIRING");
            struct stat st{}; check(::stat(domain.c_str(),&st)==0,"Cannot bind retirement fault domain");
            retirement_fault::device=st.st_dev; retirement_fault::inode=st.st_ino; retirement_fault::barrier=output.get(); retirement_fault::mode=mode;
            lease.release_verified(verified("CANCELLED_SAFE")); ::_exit(2);
        } catch(const std::exception& error) { std::cerr<<"Retirement fixture: "<<error.what()<<'\n'; ::_exit(1); }
    }
    Child child(pid); output=ure::Fd(); pollfd ready{input.get(),POLLIN,0};
    check(::poll(&ready,1,5000)>0 && (ready.revents&POLLIN),"Owner retirement did not reach the selected durability barrier"); char signal=0;
    check(::read(input.get(),&signal,1)==1 && signal=='T',"Owner retirement barrier was malformed");
    check(ure::fs::exists(domain/"owner.json")== (mode==5) && ure::fs::exists(domain/"release.json"),"Retirement did not preserve the expected completion/owner boundary"); child.kill();
}
void fresh_process_probe(const ure::fs::path& domain,const ure::fs::path& image,const ure::fs::path& journal,bool retained) {
    child_check([&] {
        ::execl("/proc/self/exe","uke-operation-lease-tests","--fresh-retirement-probe",domain.c_str(),image.c_str(),journal.c_str(),retained ? "retained" : "idle",static_cast<char*>(nullptr));
        throw std::runtime_error("Cannot execute independent retirement probe");
    });
}
void idle() {
    const auto status=ure::operation_lease_status();
    check(status["available"]==true && status["state"]=="IDLE" && status["retained_owner"]==false && status["read_only"]==true,
        "Coordinator did not return to independently observable idle state");
}
template<class First,class Second> void simultaneous(First first,Second second) {
    std::atomic<unsigned> ready{0}; std::atomic<bool> start{false}; std::array<std::exception_ptr,2> errors{};
    const auto task=[&](const auto& function,unsigned index) {
        ready.fetch_add(1,std::memory_order_release);
        while(!start.load(std::memory_order_acquire))std::this_thread::yield();
        try { function(); } catch(...) { errors[index]=std::current_exception(); }
    };
    std::jthread a([&]{task(first,0);}); std::jthread b([&]{task(second,1);});
    while(ready.load(std::memory_order_acquire)!=2)std::this_thread::yield();
    start.store(true,std::memory_order_release); a.join(); b.join();
    for(const auto& error:errors)if(error)std::rethrow_exception(error);
}
} // namespace

int main(int argc,char** argv) {
    if(argc==6 && std::string_view(argv[1])=="--fresh-retirement-probe") {
        try {
            ::unsetenv("URE_OPERATION_COORDINATOR"); ure::configure_operation_coordinator(argv[2]);
            const auto binding=binding_for(argv[3],argv[4]); const bool retained=std::string_view(argv[5])=="retained";
            check(ure::operation_lease_status()["retained_owner"]==retained,"Fresh process disagrees with the durable owner state");
            if(retained) {
                reject([&]{ure::OperationLease::acquire(binding);},"operation-recovery-required");
                reject([&]{ure::LifecycleLease::acquire("reboot");},"operation-recovery-required");
                auto recovery=ure::OperationLease::acquire(binding,ure::LeaseAdmission::RecoverSameOperation);
                check(recovery.has_retained_intent(),"Fresh process could not adopt the exact retained owner");
            } else { idle(); auto lifecycle=ure::LifecycleLease::acquire("reboot"); lifecycle.require_active(); }
            return 0;
        } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
    }
    const auto base=ure::fs::current_path(); auto pattern=(base/"ure-operation-tests-XXXXXX").string();
    std::vector<char> temporary(pattern.begin(),pattern.end()); temporary.push_back('\0'); const auto made=::mkdtemp(temporary.data());
    if(!made)return 1;
    const ure::fs::path work(made),domain=work/"coordinator",image=work/"image.bin",journal=work/"journal";
    try {
        file_bytes(image,std::string(65536,'O')); check(::mkdir(journal.c_str(),0700)==0,"Cannot create fixture journal");
        auto binding=binding_for(image,journal);
        // These children have no configured domain, so they cannot inherit an
        // already frozen parent setting. Neither case reaches target effects.
        child_check([&] {
            ::unsetenv("URE_OPERATION_COORDINATOR"); const auto status=ure::operation_lease_status();
            check(status["available"]==false && status["code"]=="ownership-unavailable","Missing domain was treated as ownership acceptance");
            reject([&]{ure::OperationLease::acquire(binding);},"ownership-unavailable");
        });
        child_check([&] {
            ::unsetenv("URE_OPERATION_COORDINATOR");
            const auto tmp=ure::fs::path("/tmp")/("ure-operation-tmpfs-"+std::to_string(::getpid()));
            ure::configure_operation_coordinator(tmp); struct statfs fsinfo{};
            check(::statfs("/tmp",&fsinfo)==0,"Cannot inspect volatile fixture filesystem");
            if(fsinfo.f_type==0x01021994L) {
                reject([&]{ure::OperationLease::acquire(binding);},"ownership-unavailable");
                check(!ure::fs::exists(tmp/"owner.json") && !ure::fs::exists(tmp/"domain.json"),"Volatile domain published retained ownership");
            }
            ure::fs::remove_all(tmp);
        });
        ::unsetenv("URE_OPERATION_COORDINATOR"); ure::configure_operation_coordinator(domain);
        const auto before=ure::operation_lease_status();
        check(before["available"]==false && !ure::fs::exists(domain),"Read-only status created the coordinator");
        {
            auto lease=ure::OperationLease::acquire(binding); lease.require_binding(binding);
            check(!lease.has_retained_intent(),"New preparation lease reported retained target intent");
            check(!ure::fs::exists(domain/"owner.json"),"Admission published intent before an effect checkpoint");
            child_check([&] {
                auto different=binding; different.journal["other_journal"]=true;
                reject([&]{ure::OperationLease::acquire(different);},"operation-busy");
                reject([&]{ure::LifecycleLease::acquire("reboot");},"operation-busy");
                reject([&]{lease.require_active();},"operation-lease-inactive");
            });
        }
        idle();
        {
            auto lease=ure::OperationLease::acquire(binding);
            simultaneous([&]{lease.checkpoint("FIRST_WORKER_INTENT");},[&]{lease.checkpoint("SECOND_WORKER_INTENT");});
            check(lease.has_retained_intent(),"Concurrent checkpoints lost retained owner state");
            // Both orders are valid, but retirement must not race a checkpoint
            // into recreating owner.json after exclusion has been released.
            simultaneous([&]{lease.release_verified(verified("CANCELLED_SAFE"));},[&]{
                try { lease.checkpoint("FINAL_WORKER_INTENT"); }
                catch(const ure::Error& error) { check(error.code=="operation-lease-inactive","Concurrent retirement had an unexpected checkpoint refusal"); }
            });
        }
        idle();
        ure::fs::rename(domain/"last-terminal.json",domain/"saved-terminal.json");
        check(ure::operation_lease_status()["available"]==false,"Released tombstone without its exact terminal receipt was treated as idle");
        reject([&]{ure::OperationLease::acquire(binding);},"unsafe-operation-coordinator");
        ure::fs::rename(domain/"saved-terminal.json",domain/"last-terminal.json");
        idle();
        bool admitted=false;
        try { auto lease=ure::OperationLease::acquire(binding); admitted=true; throw std::runtime_error("safe preparation failure"); }
        catch(const std::runtime_error&) {}
        check(admitted,"Safe preparation exception did not hold an actual lease");
        idle();
        reject([&]{ure::OperationLease::acquire(binding,ure::LeaseAdmission::RecoverSameOperation);},"operation-owner-missing");
        {
            auto lease=ure::OperationLease::acquire(binding,ure::LeaseAdmission::RecoverOrNewSameOperation);
            lease.require_active();
        }
        idle();
        reject([&]{ure::configure_operation_coordinator(work/"second-domain");},"operation-coordinator-configured");
        check(::setenv("URE_OPERATION_COORDINATOR","/tmp/ignored-after-first-use",1)==0,"Cannot set host startup override fixture");
        { auto lease=ure::OperationLease::acquire(binding); lease.require_active(); }
        idle();
        {
            auto lease=ure::OperationLease::acquire(binding); lease.checkpoint("PREPARING_EFFECT");
            check(lease.has_retained_intent(),"Published checkpoint did not report retained target intent");
            auto wrong=binding; wrong.plan_sha256=ure::sha256("other-plan");
            reject([&]{ure::OwnerControlLease::acquire(wrong);},"operation-owner-mismatch");
            {
                auto control=ure::OwnerControlLease::acquire(binding); control.require_binding(binding);
                reject([&]{lease.release_verified(verified());},"operation-control-busy");
                child_check([&]{reject([&]{ure::OwnerControlLease::acquire(binding);},"operation-control-busy");});
            }
            auto bad=verified(); bad["cleanup_complete"]=false;
            reject([&]{lease.release_verified(bad);},"operation-verification-required");
            bad=verified(); bad["verified"]=false;
            reject([&]{lease.release_verified(bad);},"operation-verification-required");
            lease.release_verified(verified("CANCELLED_SAFE"));
            reject([&]{lease.require_active();},"operation-lease-inactive");
            reject([&]{lease.has_retained_intent();},"operation-lease-inactive");
        }
        idle();
        try { auto lease=ure::OperationLease::acquire(binding); lease.checkpoint("INTENT"); throw std::runtime_error("effect uncertainty"); }
        catch(const std::runtime_error&) {}
        reject([&]{ure::OperationLease::acquire(binding);},"operation-recovery-required");
        {
            auto lease=ure::OperationLease::acquire(binding,ure::LeaseAdmission::RecoverSameOperation);
            check(lease.has_retained_intent(),"Exact recovery did not adopt retained target intent");
            lease.release_verified(verified("FAILED_SAFE"));
        }
        idle();
        interrupted_writer(binding,image);
        check(file_hash(image)==ure::sha256(std::string(32768,'N')+std::string(32768,'O')),"Interruption byte oracle did not match the durable frontier");
        const auto interrupted=ure::operation_lease_status();
        check(interrupted["available"]==true && interrupted["active_exclusion"]==false && interrupted["retained_owner"]==true,
            "Kernel lock release after SIGKILL discarded durable owner state");
        auto wrong=binding; wrong.operation_id="unrelated-operation";
        reject([&]{ure::OperationLease::acquire(wrong);},"operation-recovery-required");
        reject([&]{ure::OperationLease::acquire(wrong,ure::LeaseAdmission::RecoverSameOperation);},"operation-owner-mismatch");
        wrong=binding; wrong.journal["file_inode"]=Json::UInt64(wrong.journal["file_inode"].asUInt64()+1);
        reject([&]{ure::OperationLease::acquire(wrong,ure::LeaseAdmission::RecoverOrNewSameOperation);},"operation-owner-mismatch");
        wrong=binding; wrong.targets[0]["file_inode"]=Json::UInt64(wrong.targets[0]["file_inode"].asUInt64()+1);
        reject([&]{ure::OperationLease::acquire(wrong,ure::LeaseAdmission::RecoverSameOperation);},"operation-owner-mismatch");
        ure::fs::rename(image,work/"retained-original-image"); file_bytes(image,std::string(65536,'F'));
        wrong=binding_for(image,journal);
        reject([&]{ure::OperationLease::acquire(wrong,ure::LeaseAdmission::RecoverSameOperation);},"operation-owner-mismatch");
        check(file_hash(image)==ure::sha256(std::string(65536,'F')),"Wrong replacement target changed during refusal");
        ure::fs::remove(image); ure::fs::rename(work/"retained-original-image",image);
        ure::fs::rename(journal,work/"retained-original-journal"); check(::mkdir(journal.c_str(),0700)==0,"Cannot create replacement journal");
        wrong=binding_for(image,journal);
        reject([&]{ure::OperationLease::acquire(wrong,ure::LeaseAdmission::RecoverSameOperation);},"operation-owner-mismatch");
        ure::fs::remove(journal); ure::fs::rename(work/"retained-original-journal",journal);
        {
            auto control=ure::OwnerControlLease::acquire(binding);
            child_check([&] {
                reject([&]{ure::OperationLease::acquire(binding,ure::LeaseAdmission::RecoverSameOperation);},"operation-control-busy");
                reject([&]{ure::LifecycleLease::acquire("unmount");},"operation-control-busy");
            });
            control.require_active();
        }
        reject([&]{ure::LifecycleLease::acquire("unmount");},"operation-recovery-required");
        {
            auto lease=ure::OperationLease::acquire(binding,ure::LeaseAdmission::RecoverSameOperation);
            // Parsed and generated unsigned/signed JSON numbers compare by the
            // same canonical serialization, not JsonCpp's internal tag.
            auto equivalent=binding;
            equivalent.targets[0]["file_inode"]=Json::Int64(equivalent.targets[0]["file_inode"].asInt64());
            lease.require_binding(equivalent); lease.checkpoint("ROLLBACK_REQUIRED");
            file_bytes(image,std::string(65536,'O'));
            check(file_hash(image)==ure::sha256(std::string(65536,'O')),"Independent rollback byte verification failed");
            lease.release_verified(verified("ROLLED_BACK"));
        }
        idle();
        {
            auto lease=ure::OperationLease::acquire(binding); lease.checkpoint("FAULTED_RETIREMENT");
            struct stat st{}; check(::stat(domain.c_str(),&st)==0,"Cannot select retirement fsync fault domain");
            retirement_fault::device=st.st_dev; retirement_fault::inode=st.st_ino; retirement_fault::mode=1;
            reject([&]{lease.release_verified(verified("CANCELLED_SAFE"));},"uncertain-owner-release");
            check(retirement_fault::mode==0,"Retirement fsync fault was not exercised");
        }
        check(ure::operation_lease_status()["retained_owner"]==true,"Failed retirement fsync published false idle state");
        reject([&]{ure::OperationLease::acquire(binding);},"operation-recovery-required");
        {
            auto lease=ure::OperationLease::acquire(binding,ure::LeaseAdmission::RecoverSameOperation);
            check(file_hash(image)==ure::sha256(std::string(65536,'O')),"Retirement fault changed independently verified bytes");
            lease.release_verified(verified("CANCELLED_SAFE"));
        }
        idle();
        child_check([&] {
            auto lease=ure::OperationLease::acquire(binding); lease.checkpoint("ALL_RESTORATION_FAILED");
            const auto original=ure::json_file(domain/"owner.json"); struct stat st{};
            check(::stat(domain.c_str(),&st)==0,"Cannot select completion publication fault domain");
            retirement_fault::device=st.st_dev; retirement_fault::inode=st.st_ino; retirement_fault::mode=3;
            reject([&]{lease.release_verified(verified("CANCELLED_SAFE"));},"uncertain-owner-release");
            check(retirement_fault::mode==4,"Completion publication and restoration faults were not exercised");
            retirement_fault::mode=0;
            check(ure::json(ure::json_file(domain/"owner.json"))==ure::json(original),"Failed completion publication removed or changed the original owner");
        });
        fresh_process_probe(domain,image,journal,true);
        { auto lease=ure::OperationLease::acquire(binding,ure::LeaseAdmission::RecoverSameOperation); lease.release_verified(verified("CANCELLED_SAFE")); }
        idle();
        interrupted_retirement(binding,domain,5);
        check(ure::operation_lease_status()["retained_owner"]==true,"SIGKILL before completion durability discarded original ownership");
        fresh_process_probe(domain,image,journal,true);
        reject([&]{ure::OperationLease::acquire(binding);},"operation-recovery-required");
        {
            auto lease=ure::OperationLease::acquire(binding,ure::LeaseAdmission::RecoverSameOperation);
            check(file_hash(image)==ure::sha256(std::string(65536,'O')),"Interrupted retirement changed independently verified bytes");
            lease.release_verified(verified("CANCELLED_SAFE"));
        }
        idle();
        interrupted_retirement(binding,domain,2);
        check(file_hash(image)==ure::sha256(std::string(65536,'O')),"Post-completion interruption changed verified bytes");
        // The terminal receipt and RELEASED marker were both synced before
        // owner unlink. An interrupted unlink sync is safe to observe as idle
        // only when no original owner remains and that exact receipt exists.
        fresh_process_probe(domain,image,journal,false);
        idle();
        {
            auto lifecycle=ure::LifecycleLease::acquire("unmount"); lifecycle.require_active();
            child_check([&] {
                reject([&]{ure::OperationLease::acquire(binding);},"operation-busy");
                reject([&]{ure::LifecycleLease::acquire("reboot");},"gui-lifecycle-busy");
                reject([&]{lifecycle.require_active();},"operation-lease-inactive");
            });
            check(ure::operation_lease_status()["active_exclusion"]==true,"Lifecycle exclusion was released before the simulated effect");
        }
        idle();
        {
            auto lease=ure::OperationLease::acquire(binding);
            ure::fs::rename(domain/"operation.lock",domain/"saved-operation.lock"); file_bytes(domain/"operation.lock","");
            reject([&]{lease.require_active();},"changed-operation-coordinator");
            child_check([&]{reject([&]{ure::OperationLease::acquire(binding);},"changed-operation-coordinator");});
            ure::fs::remove(domain/"operation.lock"); ure::fs::rename(domain/"saved-operation.lock",domain/"operation.lock"); lease.require_active();
            ure::fs::rename(domain,work/"saved-domain"); check(::mkdir(domain.c_str(),0700)==0,"Cannot create replaced coordinator fixture");
            reject([&]{lease.require_active();},"changed-operation-coordinator");
            child_check([&]{reject([&]{ure::OperationLease::acquire(binding);},"changed-operation-coordinator");});
            check(ure::fs::is_empty(domain),"Replaced configured directory was populated before refusal");
            ure::fs::remove(domain); ure::fs::rename(work/"saved-domain",domain); lease.require_active();
            ure::fs::rename(domain/"operation.lock",domain/"saved-operation.lock");
            check(::symlink("saved-operation.lock",(domain/"operation.lock").c_str())==0,"Cannot create lock symlink fixture");
            reject([&]{lease.require_active();},"unsafe-operation-coordinator");
            ure::fs::remove(domain/"operation.lock"); ure::fs::rename(domain/"saved-operation.lock",domain/"operation.lock");
            check(::link((domain/"operation.lock").c_str(),(domain/"extra-hardlink").c_str())==0,"Cannot create multiple-link fixture");
            reject([&]{lease.require_active();},"unsafe-operation-coordinator"); ure::fs::remove(domain/"extra-hardlink");
            check(::chmod(domain.c_str(),0755)==0,"Cannot change coordinator mode fixture");
            reject([&]{lease.require_active();},"unsafe-operation-coordinator"); check(::chmod(domain.c_str(),0700)==0,"Cannot restore coordinator mode");
            check(::chmod((domain/"operation.lock").c_str(),0644)==0,"Cannot change lock mode fixture");
            reject([&]{lease.require_active();},"unsafe-operation-coordinator"); check(::chmod((domain/"operation.lock").c_str(),0600)==0,"Cannot restore lock mode");
            lease.require_active();
        }
        idle();
        {
            auto lease=ure::OperationLease::acquire(binding); lease.checkpoint("VALIDATING_OWNER");
            ure::fs::rename(domain/"owner.json",domain/"saved-owner.json"); file_bytes(domain/"owner.json","{");
            check(ure::operation_lease_status()["available"]==false,"Truncated owner JSON was treated as safe idle state");
            reject([&]{lease.require_active();},"invalid-json");
            ure::fs::remove(domain/"owner.json"); ure::fs::rename(domain/"saved-owner.json",domain/"owner.json");
            lease.release_verified(verified("FAILED_SAFE"));
        }
        idle();
        auto invalid=binding; invalid.targets[0]["unexpected_float"]=1.5;
        reject([&]{ure::OperationLease::acquire(invalid);},"invalid-operation-binding");
        invalid=binding; invalid.journal["huge"]=std::string(4097,'x');
        reject([&]{ure::OperationLease::acquire(invalid);},"invalid-operation-binding");
        invalid=binding; invalid.targets=ure::Value(Json::arrayValue);
        for(unsigned i=0;i<129;++i)invalid.targets.append(binding.targets[0]);
        reject([&]{ure::OperationLease::acquire(invalid);},"invalid-operation-binding");
        reject([&]{ure::LifecycleLease::acquire("arbitrary-command");},"invalid-lifecycle-action");
        idle();
        check(file_hash(image)==ure::sha256(std::string(65536,'O')),"Negative ownership controls changed fixture target bytes");
        const auto terminal=ure::json_file(domain/"last-terminal.json");
        check(terminal["terminal"]["verified"]==true && terminal["terminal"]["cleanup_complete"]==true,"Verified terminal receipt was not retained");
        ure::fs::remove_all(work);
        std::cout<<"PASS cooperative operation leases: independent-process contention, concurrent-token checkpoint/retirement, pre-intent release, durable SIGKILL ownership, exact journal/plan/target recovery, owner-bound controls, verified cleanup, lifecycle exclusion, directory/lock replacement, symlink/hardlink/privacy refusals, completion and restoration fsync failures, independent exec recovery probes, pre/post-completion SIGKILL boundaries, exact terminal receipts, and read-only status; disposable host files only\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
