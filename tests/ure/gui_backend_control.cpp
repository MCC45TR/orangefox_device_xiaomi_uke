// SPDX-License-Identifier: Apache-2.0
// Actual GUI/native rescue controls with a paused pre-unshare host-link fixture.
// No installed payload, real namespace, mount, cgroup enforcement or tablet.
#include "management-hooks.h"
#include "operation_lease.hpp"
#include "lifecycle_policy.hpp"
#include "rescue_resources.hpp"
#include <fstream>
#include <iostream>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
namespace {
int namespace_marker=-1;
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
std::string value(const char* key) { std::string text; DataManager::GetValue(key,text); return text; }
void write(const ure::fs::path& path,const std::string& text) {
    ure::fs::create_directories(path.parent_path()); std::ofstream file(path,std::ios::binary);
    file.write(text.data(),static_cast<std::streamsize>(text.size())); check(file.good(),"Cannot create private rescue-control fixture"); file.close();
}
class FixtureResources final:public ure::RescueResources {
    ure::Fd process_;
    bool attached_=false,released_=false;
public:
    void attach_worker(pid_t process) override {
        process_=ure::Fd(static_cast<int>(::syscall(SYS_pidfd_open,process,0))); check(process_.get()>=0,"Cannot retain exact fixture worker"); attached_=true;
    }
    void close_in_child() noexcept override { process_=ure::Fd(); released_=true; }
    ure::Value observation() const override {
        ure::Value result; result["backend"]="host-link-fixture"; result["aggregate_limits_enforced"]=false;
        result["worker_attachment_verified"]=attached_; result["group_empty_verified"]=released_; result["group_removed_verified"]=released_; return result;
    }
    void kill_all() noexcept override { if(process_.get()>=0)static_cast<void>(::syscall(SYS_pidfd_send_signal,process_.get(),SIGKILL,nullptr,0)); }
    bool finish() noexcept override {
        if(attached_) { pollfd exited{process_.get(),POLLIN,0}; if(::poll(&exited,1,0)!=1 || !(exited.revents&POLLIN))return false; }
        released_=true; return true;
    }
    ~FixtureResources() override { if(!released_) { kill_all(); static_cast<void>(finish()); } }
};
void wait_worker(int marker) {
    pollfd ready{marker,POLLIN,0}; check(::poll(&ready,1,4000)==1 && (ready.revents&POLLIN),"Native supervisor never reached its pre-unshare fixture");
    char byte=0; check(::read(marker,&byte,1)==1 && byte=='N',"Wrong namespace fixture marker");
}
void collect_all(GUIAction& action) {
    const auto deadline=ure::monotonic_ms()+10000;
    while(ure::monotonic_ms()<deadline) {
        static_cast<void>(action.uremanager("job-collect")); static_cast<void>(action.uremanager("job-status"));
        if(value("ure_job_active")=="0" && value("ure_job_result_pending")!="1")return;
        ::poll(nullptr,0,2);
    }
    throw std::runtime_error("Exact GUI backend control did not finish within its fixture deadline");
}
}
extern "C" ure::RescueResources* __wrap_ure_create_rescue_resources(const ure::Value* policy,const char* id) {
    check(policy && (*policy)["aggregate_required"]==true && id && std::strlen(id)==32,"Unbound native resource request"); return new FixtureResources;
}
extern "C" int __wrap_unshare(int) {
    if(namespace_marker>=0)static_cast<void>(::write(namespace_marker,"N",1));
    for(;;)::pause();
}
extern "C" int __wrap_mount(const char*,const char*,const char*,unsigned long,const void*) { ::_exit(90); }
int main(int argc,char** argv) {
    auto pattern=(ure::fs::path(argc>1 ? argv[1] : ".")/"ure-gui-controller-XXXXXX").string();
    std::vector<char> text(pattern.begin(),pattern.end()); text.push_back('\0'); const auto* made=::mkdtemp(text.data()); if(!made)return 1;
    const ure::fs::path work(made);
    try {
        const auto root=work/"root"; write(root/"etc/os-release","ID=arch\nNAME=Arch fixture\n"); write(root/"etc/marker","original\n");
        std::string elf(20,'\0'); elf.replace(0,4,"\x7f" "ELF"); elf[4]=2; elf[5]=1; struct utsname host{};
        check(::uname(&host)==0,"Cannot inspect fixture architecture"); elf[18]=static_cast<char>(std::string(host.machine)=="aarch64" ? 183 : 62);
        write(root/"usr/bin/bash",elf); check(::chmod((root/"usr/bin/bash").c_str(),0700)==0,"Cannot set fixture executable permission");
        for(const auto* directory:{"proc","sys","dev","tmp","run"})ure::fs::create_directories(root/directory);
        for(const auto& [key,setting]:std::map<std::string,std::string>{{"ure_root",root.string()},{"ure_esp",""},{"ure_journal_parent",work.string()},
            {"ure_rescue_action","shell"},{"ure_rescue_write","0"},{"ure_rescue_timeout","8"},{"ure_rescue_command","exit 0"}})DataManager::SetValue(key,setting);
        GUIAction action; check(run_management(action,"rescue-plan")==0,"Actual GUI rescue review failed");
        const auto journal=ure::fs::path(ure::parse_json(value("ure_output"))["journal_directory"].asString()); const auto hash=value("ure_manage_hash");
        int marker[2]{}; check(::pipe2(marker,O_CLOEXEC|O_NONBLOCK)==0,"Cannot create controller lifetime marker");
        ure::Fd reader(marker[0]),writer(marker[1]); namespace_marker=writer.get();
        check(action.uremanager("rescue-execute")==0,"Cannot queue actual GUI rescue execution"); wait_worker(reader.get());
        const auto id=value("ure_job_id"); check(action.uremanager("job-status")==0 && value("ure_job_backend_owner_id")==id,"GUI lost its exact backend identity");
        { ure::LegacyLifecycleGuard reboot("reboot"); check(!reboot.active(),"Running native GUI rescue allowed a reboot"); }
        DataManager::SetValue("ure_job_id",std::string(32,'0')); check(action.uremanager("job-cancel")==1 && !ure::fs::exists(journal/"cancel.json"),"Wrong GUI job identity changed the cancellation journal");
        DataManager::SetValue("ure_job_id",id); DataManager::SetValue("ure_root",(work/"unopened-root").string());
        DataManager::SetValue("ure_manage_journal",(work/"unopened-journal").string()); DataManager::SetValue("ure_manage_hash",std::string(64,'0'));
        check(action.uremanager("job-view-changed")==0,"Cannot invalidate the old GUI view");
        const auto before=ure::monotonic_ms(); check(action.uremanager("job-cancel")==0 && ure::monotonic_ms()-before<250,"Controller admission waited for native work");
        const auto ack=ure::parse_json(value("ure_job_cancel_ack"));
        check(ack["acknowledgement"]=="ADVISORY_FLAG_ONLY" && ack["backend_controller"]["controlled_job_id"]==id &&
            ack["backend_controller"]["plan_sha256"]==hash && ack["backend_controller"]["backend_cleanup_verified"]==false,"GUI controller borrowed changed selections or claimed cleanup");
        collect_all(action);
        const auto final=ure::parse_json(ure::Root(journal).read("state.json")); const auto control=ure::parse_json(value("ure_job_control_result"));
        check(final["state"]=="CANCELLED" && final["cancel_requested"]==true && final["ownership_lifetime_verified"]==true && final["cleanup_pending"]==false,
            "Native supervisor did not verify exact requested lifetime cleanup");
        check(control["output"]["state"]=="CANCEL_REQUESTED" && control["output"]["cleanup_complete"]==false &&
            control["output"]["controlled_job_id"]==id,"Native control acknowledgement was replaced by fabricated completion");
        check(value("ure_root")== (work/"unopened-root").string() && !ure::fs::exists(work/"unopened-root") && !ure::fs::exists(work/"unopened-journal"),"Old GUI work retargeted or republished into changed selections");
        check(ure::operation_lease_status()["state"]=="IDLE" && ure::runtime_active_jobs()==0,"Verified GUI cancellation retained operation/activity ownership");
        DataManager::SetValue("ure_root",root.string()); DataManager::SetValue("ure_manage_hash",""); DataManager::SetValue("ure_manage_journal","");
        check(run_management(action,"rescue-plan")==0,"Cannot review joined teardown scenario");
        const auto closing_journal=ure::fs::path(ure::parse_json(value("ure_output"))["journal_directory"].asString());
        check(action.uremanager("rescue-execute")==0,"Cannot queue joined teardown scenario"); wait_worker(reader.get());
        ure_gui_shutdown_jobs();
        const auto closed=ure::parse_json(ure::Root(closing_journal).read("state.json"));
        check(closed["state"]=="CANCELLED" && closed["ownership_lifetime_verified"]==true && ure::runtime_active_jobs()==0 &&
            ure::operation_lease_status()["state"]=="IDLE","Application teardown cancelled its controller or left an owned supervisor running");
        check(action.uremanager("rescue-plan")==1,"Application teardown admitted another worker");
        check(management_foreign_reads==0 && management_foreign_writes==0,"Backend controller accessed GUI state from a worker");
        check(ure::Root(root).read("etc/marker")=="original\n","Controller or teardown changed fixture root bytes");
        namespace_marker=-1; ure::fs::remove_all(work);
        std::cout<<"PASS actual GUI/native rescue controllers: exact job/plan/root binding, changed-view refusal, responsive admission, durable cancellation, verified supervisor closure and joined application teardown; pre-unshare host-link fixture only\n"; return 0;
    } catch(const std::exception& error) {
        ure_gui_shutdown_jobs(); std::cerr<<error.what()<<"\nPrivate GUI controller fixture retained: "<<work<<'\n'; return 1;
    }
}
