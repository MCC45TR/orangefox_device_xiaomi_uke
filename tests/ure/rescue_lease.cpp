// SPDX-License-Identifier: Apache-2.0
// No installed OS, payload execution, real mount or unshare in this fixture.
#include "uke.h"
#include "operation_lease.hpp"
#include "operation_guard.hpp"
#include "rescue_resources.hpp"
#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#include <sys/utsname.h>
#include <unistd.h>

namespace {
bool fail_fork=false;
enum class Fault { None,Unavailable,Attach,Timeout,Cancel,Cleanup };
Fault fault=Fault::None;
int namespace_marker=-1;
unsigned factories=0,attachments=0,finishes=0;
bool cleanup_verified=false;
void check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
void write_file(const ure::fs::path& path,const std::string& bytes) {
    ure::fs::create_directories(path.parent_path()); std::ofstream stream(path,std::ios::binary);
    stream.write(bytes.data(),static_cast<std::streamsize>(bytes.size())); check(stream.good(),"Cannot write private rescue fixture"); stream.close();
}
class FixtureResources final:public ure::RescueResources {
    ure::Fd process_;
    bool attached_=false,released_=false;
public:
    void attach_worker(pid_t pid) override {
        ++attachments;
        ure::require(fault!=Fault::Attach,"rescue-resource-unavailable","Injected admission attachment refusal");
        process_=ure::Fd(static_cast<int>(::syscall(SYS_pidfd_open,pid,0)));
        check(process_.get()>=0,"Cannot bind fixture worker lifetime"); attached_=true;
    }
    void close_in_child() noexcept override { process_=ure::Fd(); released_=true; }
    ure::Value observation() const override {
        ure::Value out; out["backend"]="host-link-fixture"; out["aggregate_limits_enforced"]=false;
        out["worker_attachment_verified"]=attached_; out["group_empty_verified"]=released_;
        out["group_removed_verified"]=released_; return out;
    }
    void kill_all() noexcept override { if(process_.get()>=0)static_cast<void>(::syscall(SYS_pidfd_send_signal,process_.get(),SIGKILL,nullptr,0)); }
    bool finish() noexcept override {
        ++finishes;
        if(fault==Fault::Cleanup && finishes==1)return false;
        if(attached_) { pollfd exited{process_.get(),POLLIN,0}; if(::poll(&exited,1,0)!=1 || !(exited.revents&POLLIN))return false; }
        released_=true; cleanup_verified=true; return true;
    }
    ~FixtureResources() override { if(!released_) { kill_all(); static_cast<void>(finish()); } }
};
}
extern "C" ure::RescueResources* __wrap_ure_create_rescue_resources(const ure::Value* policy,const char* id) {
    ++factories; check(policy && (*policy)["aggregate_required"]==true && id && std::string(id).size()==32,"Unbound resource factory request");
    ure::require(fault!=Fault::Unavailable,"rescue-resource-unavailable","Injected missing aggregate controller");
    return new FixtureResources;
}
extern "C" pid_t __real_fork();
extern "C" pid_t __wrap_fork() { if(fail_fork) { errno=EAGAIN; return -1; } return __real_fork(); }
extern "C" int __wrap_unshare(int) {
    if(namespace_marker>=0)static_cast<void>(::write(namespace_marker,"N",1));
    // A paused worker has created no namespace, mount, init or payload. These
    // controls exercise the real supervisor deadline and durable cancellation.
    if(fault==Fault::Timeout || fault==Fault::Cancel)for(;;)::pause();
    errno=EPERM; return -1;
}
extern "C" int __wrap_mount(const char*,const char*,const char*,unsigned long,const void*) { ::_exit(90); }
int main(int argc,char** argv) {
    auto pattern=(ure::fs::path(argc>1 ? argv[1] : ".")/"ure-rescue-ownership-XXXXXX").string();
    std::vector<char> name(pattern.begin(),pattern.end()); name.push_back('\0'); const auto made=::mkdtemp(name.data()); if(!made)return 1;
    const ure::fs::path work(made);
    try {
        const auto root_path=work/"root"; write_file(root_path/"etc/os-release","ID=arch\nNAME=Arch fixture\n");
        write_file(root_path/"etc/fstab","UUID=fixture-root / ext4 defaults 0 1\n"); write_file(root_path/"etc/marker","original contents\n");
        // This deliberately incomplete header is only plan metadata. unshare
        // is refused before init/payload creation, so it is never executed.
        std::string elf(20,'\0'); elf.replace(0,4,"\x7f" "ELF"); elf[4]=2; elf[5]=1; struct utsname host{};
        check(::uname(&host)==0,"Cannot inspect host fixture architecture"); const auto machine=std::string(host.machine)=="aarch64" ? 183 : 62;
        elf[18]=static_cast<char>(machine); write_file(root_path/"usr/bin/bash",elf);
        check(::chmod((root_path/"usr/bin/bash").c_str(),0700)==0,"Cannot set fixture executable permission");
        for(const auto* directory:{"proc","sys","dev","run","tmp"})ure::fs::create_directories(root_path/directory);
        ure::Root root(root_path); const auto original=ure::sha256(root.read("etc/marker"));
        auto policy=ure::rescue_resource_policy(ure::Value(Json::objectValue));
        check(policy["memory_max_bytes"].asUInt64()==1024ULL*1024*1024 && policy["pids_max"].asUInt()==128 &&
            policy["job_limit"].asUInt()==2 && policy["tmpfs_bytes"].asUInt64()==256ULL*1024*1024 &&
            policy["unbounded_fallback_accepted"]==false && policy["rlimit_nproc_fallback_accepted"]==false,"Wrong default aggregate policy");
        for(const auto* key:{"memory_mib","jobs","pids","controller_path","backend"})for(const auto& value:{ure::Value(-1),ure::Value(0),ure::Value(999999),ure::Value(1.5),ure::Value("128"),ure::Value(true)}) {
            ure::Value bad; bad["resources"][key]=value; bool rejected=false;
            try { static_cast<void>(ure::rescue_resource_policy(bad)); } catch(const ure::Error& error) { rejected=error.code=="invalid-rescue-resources"; }
            check(rejected,"Invalid or caller-selected resource backend was accepted");
        }
        for(const bool writable:{false,true})for(const bool failed_fork:{false,true}) {
            ure::Value request; request["schema"]=1; request["action"]="shell"; request["write"]=writable; request["network"]=false;
            request["timeout_seconds"]=5; request["shell_input"]="exit 0\n"; const auto plan=ure::linux_rescue_plan(root,request);
            const auto journal=work/(std::string(writable ? "writable" : "readonly")+(failed_fork ? "-fork" : "-namespace"));
            fail_fork=failed_fork; const auto result=ure::linux_rescue_execute(root,plan,journal,plan["plan_sha256"].asString()); fail_fork=false;
            check(result["successful"]==false && result["session_init_started"]==false && result["session_mounts_released"]==true &&
                result["cleanup_pending"]==false && result["ownership_lifetime_verified"]==true && result["operation_owner_released"]==true,
                "Pre-init/fork failure did not prove and release its native lifetime");
            check(result["target_contents_verified"]==false,"Lifetime closure claimed installed content validation");
            check(result["error_code"]==(failed_fork ? "process-error" : "namespace-unavailable"),"Wrong pre-init failure code");
            check(!ure::fs::exists(journal/"mount-root") && ure::sha256(root.read("etc/marker"))==original,"Failed pre-init session changed its original or retained anchor");
            check(ure::operation_lease_status()["state"]=="IDLE","Verified pre-init failure stranded shared ownership");
            check(result["resource_group_released"]==true && result["resource_enforcement"]["aggregate_limits_enforced"]==false,
                "Fixture closure claimed real kernel aggregate stress");
            { auto lifecycle=ure::LifecycleLease::acquire("reboot"); lifecycle.require_active(); }
        }
        for(const auto selected:{Fault::Unavailable,Fault::Attach,Fault::Timeout,Fault::Cancel,Fault::Cleanup}) {
            fault=selected; factories=0; attachments=0; finishes=0; cleanup_verified=false;
            int markers[2]{}; check(::pipe2(markers,O_CLOEXEC|O_NONBLOCK)==0,"Cannot create fixture namespace marker");
            ure::Fd marker_read(markers[0]),marker_write(markers[1]); namespace_marker=marker_write.get();
            ure::Value request; request["schema"]=1; request["action"]="shell"; request["write"]=false; request["network"]=false;
            request["timeout_seconds"]=selected==Fault::Timeout ? 1 : 5; request["shell_input"]="exit 0\n";
            request["resources"]["memory_mib"]=128; request["resources"]["pids"]=16; request["resources"]["jobs"]=1;
            const auto plan=ure::linux_rescue_plan(root,request); const auto journal=work/("resources-"+std::to_string(static_cast<int>(selected)));
            check(plan["resource_policy"]["memory_max_bytes"].asUInt64()==128ULL*1024*1024 && plan["ownership_targets"].size()==1,"Choices or owner targets absent from seal");
            pid_t helper=-1;
            if(selected==Fault::Cancel) {
                helper=__real_fork(); check(helper>=0,"Cannot create fixture control client");
                if(helper==0) {
                    try {
                        for(unsigned attempt=0;attempt<200;++attempt) {
                            if(ure::fs::exists(journal/"state.json")) {
                                auto store=ure::private_directory(journal,false); const auto state=ure::parse_json(store.read("state.json"));
                                if(state["state"]=="RUNNING") {
                                    bool wrong=false; try { static_cast<void>(ure::linux_rescue_cancel(journal,std::string(64,'0'))); }
                                    catch(const ure::Error& error) { wrong=error.code=="confirmation-required"; }
                                    check(wrong && !store.exists("cancel.json"),"Wrong cancellation hash changed the journal");
                                    for(const bool hardlink:{false,true}) {
                                        if(hardlink)check(::link((journal/"plan.json").c_str(),(journal/"plan-alias.json").c_str())==0,"Cannot create private hardlink control");
                                        else check(::chmod((journal/"plan.json").c_str(),0644)==0,"Cannot create unsafe permission control");
                                        bool unsafe=false; try { static_cast<void>(ure::linux_rescue_cancel(journal,plan["plan_sha256"].asString())); }
                                        catch(const ure::Error& error) { unsafe=error.code=="unsafe-rescue-journal"; }
                                        check(unsafe && !store.exists("cancel.json"),"Unsafe control record produced a cancellation request");
                                        if(hardlink)check(::unlink((journal/"plan-alias.json").c_str())==0,"Cannot remove private hardlink control");
                                        else check(::chmod((journal/"plan.json").c_str(),0600)==0,"Cannot restore private plan permission");
                                    }
                                    const auto ack=ure::linux_rescue_cancel(journal,plan["plan_sha256"].asString());
                                    check(ack["state"]=="CANCEL_REQUESTED" && ack["operation_owner_released"]==false && ack["cleanup_complete"]==false,"Cancellation claimed early closure");
                                    ::_exit(0);
                                }
                            }
                            ::poll(nullptr,0,10);
                        }
                    } catch(...) { ::_exit(91); }
                    ::_exit(92);
                }
            }
            ure::Value result;
            try { result=ure::linux_rescue_execute(root,plan,journal,plan["plan_sha256"].asString()); }
            catch(const ure::Error& error) { check(selected==Fault::Unavailable && error.code=="rescue-resource-unavailable","Unexpected resource execution exception"); }
            namespace_marker=-1;
            if(helper>0) { int status=0; check(::waitpid(helper,&status,0)==helper && WIFEXITED(status) && WEXITSTATUS(status)==0,"Owner-bound control client failed"); }
            char marker=0; const auto namespaces=::read(marker_read.get(),&marker,1);
            check(factories==1 && ure::sha256(root.read("etc/marker"))==original,"Resource admission changed original contents or recreated the backend");
            if(selected==Fault::Unavailable) {
                check(attachments==0 && namespaces<0 && !ure::fs::exists(journal),"Missing resource enforcement started a worker or journal");
            } else {
                check(attachments==1 && !ure::fs::exists(journal/"mount-root"),"Resource worker binding or anchor cleanup failed");
                if(selected==Fault::Attach)check(namespaces<0 && result["error_code"]=="rescue-resource-unavailable","Rejected attachment crossed the namespace barrier");
                else check(namespaces==1 && marker=='N',"Controlled worker never reached the namespace check");
                if(selected==Fault::Timeout || selected==Fault::Cancel)check(result["state"]==(selected==Fault::Timeout ? "TIMED_OUT" : "CANCELLED") &&
                    result["session_init_started"].isNull() && result["resource_group_lifetime_verified"]==true && result["ownership_lifetime_verified"]==true &&
                    result["operation_owner_released"]==true && result["cleanup_pending"]==false,"Timeout/cancel did not verify complete worker lifetime");
                if(selected==Fault::Cleanup) {
                    check(result["cleanup_pending"]==true && result["ownership_lifetime_verified"]==false && result["operation_owner_released"]!=true &&
                        ure::operation_lease_status()["state"]!="IDLE" && cleanup_verified,"Unverified first cleanup released the retained owner");
                    bool blocked=false; try { auto lifecycle=ure::LifecycleLease::acquire("reboot"); } catch(const ure::Error&) { blocked=true; }
                    check(blocked,"Unresolved resource cleanup allowed reboot admission");
                    // The host-only backend's later pidfd oracle independently
                    // proved emptiness. Explicit recovery, never a destructor,
                    // retires the exact retained fixture operation.
                    ure::ManagedOperation recovery(ure::operation_binding("linux.rescue",plan,journal,plan["ownership_targets"]),true);
                    ure::Value proof; proof["state"]="FAILED_SAFE"; static_cast<void>(recovery.finish(proof,true,true));
                }
            }
            check(ure::operation_lease_status()["state"]=="IDLE","Resource fixture stranded a verified or recovered owner");
        }
        fault=Fault::None;
        {
            ure::Value request; request["schema"]=1; request["action"]="shell"; request["write"]=false; request["network"]=false;
            request["timeout_seconds"]=1; request["shell_input"]="exit 0\n"; const auto plan=ure::linux_rescue_plan(root,request);
            const auto binding=ure::operation_binding("linux.rescue",plan,work/"retirement-control",plan["ownership_targets"]);
            ure::ManagedOperation operation(binding); operation.begin("CONTROL_RETIREMENT_TEST");
            auto control=std::make_unique<ure::OwnerControlLease>(ure::OwnerControlLease::acquire(binding));
            std::thread client([held=std::move(control)]() mutable { ::poll(nullptr,0,100); held.reset(); });
            const auto started=ure::monotonic_ms(); ure::Value proof; proof["state"]="FAILED_SAFE";
            try { const auto result=operation.finish(proof,true,true); check(result["operation_owner_released"]==true,"Completed control stranded terminal retirement"); }
            catch(...) { client.join(); throw; }
            client.join(); check(ure::monotonic_ms()-started>=90 && ure::operation_lease_status()["state"]=="IDLE","Short control did not serialize verified retirement");
        }
        ure::fs::remove_all(work);
        std::cout<<"PASS rescue source/host controls: strict aggregate policy, admission-before-effects, blocked-worker attachment, fork/unshare failure, timeout, owner-bound cancellation, conservative failed cleanup and exact recovery; no installed payload, kernel stress, real mount or tablet execution\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<"\nPrivate fixture retained: "<<work<<'\n'; return 1; }
}
