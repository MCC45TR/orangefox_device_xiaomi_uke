// SPDX-License-Identifier: Apache-2.0
// Production GUI and Btrfs ownership/control with exact-FD ioctl stand-ins.
// Private host files only; no real Btrfs mount or storage device operation.
#include "management-hooks.h"
#include "lifecycle_policy.hpp"
#include "operation_lease.hpp"
#include <cstdarg>
#include <fstream>
#include <iostream>
#include <linux/btrfs.h>
#include <linux/magic.h>
#include <sys/statfs.h>
namespace {
dev_t root_device=0; ino_t root_inode=0;
std::atomic<bool> running{false},entered{false},cancel{false},pause_requested{false},inject_error{false};
std::atomic<unsigned> control_calls{0};
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
std::string value(const char* key) { std::string text; DataManager::GetValue(key,text); return text; }
bool exact_root(int fd) { struct stat observed{}; return ::fstat(fd,&observed)==0 && observed.st_dev==root_device && observed.st_ino==root_inode; }
void collect_all(GUIAction& action) {
    const auto deadline=ure::monotonic_ms()+5000;
    while(ure::monotonic_ms()<deadline) {
        static_cast<void>(action.uremanager("job-collect")); static_cast<void>(action.uremanager("job-status"));
        if(value("ure_job_active")=="0" && value("ure_job_result_pending")!="1")return;
        ::poll(nullptr,0,2);
    }
    throw std::runtime_error("Btrfs controller fixture did not complete");
}
void await_native() {
    const auto deadline=ure::monotonic_ms()+5000;
    while(!entered && ure::monotonic_ms()<deadline)::poll(nullptr,0,2);
    check(entered,"Production Btrfs executor did not enter its exact-root ioctl fixture");
}
void write(const ure::fs::path& path,const char* contents) { std::ofstream file(path); file<<contents; check(file.good(),"Cannot write fixture marker"); }
}
extern "C" int __real_fstatfs(int,struct statfs*);
extern "C" int __wrap_fstatfs(int fd,struct statfs* info) {
    const auto result=__real_fstatfs(fd,info); if(result==0 && exact_root(fd))info->f_type=BTRFS_SUPER_MAGIC; return result;
}
extern "C" int __wrap_ioctl(int fd,unsigned long request,...) {
    if(!exact_root(fd)) { errno=ENOTTY; return -1; }
    va_list arguments; va_start(arguments,request);
    if(request==BTRFS_IOC_BALANCE_CTL) {
        const auto action=va_arg(arguments,int); va_end(arguments); ++control_calls;
        if(action==BTRFS_BALANCE_CTL_PAUSE)pause_requested=true;
        else if(action==BTRFS_BALANCE_CTL_CANCEL) { cancel=true; running=false; }
        else { errno=EINVAL; return -1; }
        return 0;
    }
    if(request==BTRFS_IOC_SCRUB_CANCEL) { va_end(arguments); ++control_calls; cancel=true; running=false; return 0; }
    auto* payload=va_arg(arguments,void*); va_end(arguments);
    if(request==BTRFS_IOC_FS_INFO) {
        auto* info=static_cast<btrfs_ioctl_fs_info_args*>(payload); std::memset(info,0,sizeof(*info));
        info->num_devices=1; info->max_id=1; std::memset(info->fsid,0x42,sizeof(info->fsid)); return 0;
    }
    if(request==BTRFS_IOC_DEV_INFO) { auto* info=static_cast<btrfs_ioctl_dev_info_args*>(payload); info->total_bytes=512*1024*1024; return 0; }
    if(request==BTRFS_IOC_BALANCE_PROGRESS || request==BTRFS_IOC_SCRUB_PROGRESS) {
        if(running)return 0;
        errno=ENOTCONN; return -1;
    }
    if(request==BTRFS_IOC_SCRUB || request==BTRFS_IOC_BALANCE_V2) {
        running=true; entered=true;
        while(!cancel && !pause_requested)::poll(nullptr,0,2);
        if(cancel)running=false;
        errno=inject_error ? EIO : ECANCELED; return -1;
    }
    errno=ENOTTY; return -1;
}
int main(int argc,char** argv) {
    auto pattern=(ure::fs::path(argc>1 ? argv[1] : ".")/"ure-gui-btrfs-control-XXXXXX").string();
    std::vector<char> text(pattern.begin(),pattern.end()); text.push_back('\0'); const auto* made=::mkdtemp(text.data()); if(!made)return 1;
    const ure::fs::path work(made);
    try {
        GUIAction action;
        for(const auto scenario:{0,1,2}) {
            const auto root=work/("root-"+std::to_string(scenario)); ure::fs::create_directory(root); write(root/"marker","original\n");
            struct stat observed{}; check(::stat(root.c_str(),&observed)==0,"Cannot identify private root fixture"); root_device=observed.st_dev; root_inode=observed.st_ino;
            running=false; entered=false; cancel=false; pause_requested=false; inject_error=scenario==2;
            for(const auto& [key,setting]:std::map<std::string,std::string>{{"ure_btrfs_root",root.string()},{"ure_journal_parent",work.string()},
                {"ure_btrfs_action",scenario==1 ? "balance" : "scrub"},{"ure_btrfs_device","1"},{"ure_btrfs_repair","1"},{"ure_btrfs_usage","10"},{"ure_btrfs_limit","1"}})DataManager::SetValue(key,setting);
            check(run_management(action,"btrfs-plan")==0,"Actual Btrfs GUI review refused the private ioctl fixture");
            const auto review=ure::parse_json(value("ure_output")); const ure::fs::path journal=review["journal_directory"].asString();
            check(action.uremanager("btrfs-execute")==0,"Cannot start actual GUI Btrfs maintenance"); await_native();
            check(action.uremanager("job-status")==0,"Cannot inspect global Btrfs activity"); const auto id=value("ure_job_id");
            { ure::LegacyLifecycleGuard unmount("unmount"); check(!unmount.active(),"Running Btrfs GUI work allowed unmount"); }
            DataManager::SetValue("ure_btrfs_root",(work/"unopened-root").string()); DataManager::SetValue("ure_journal_parent",(work/"unopened-parent").string());
            check(action.uremanager("job-view-changed")==0 && action.uremanager("linux-audit")==1,"Changed-view navigation admitted another worker during maintenance");
            const auto selected=scenario==1 ? "balance-pause" : "scrub-cancel"; DataManager::SetValue("ure_btrfs_action",selected);
            check(action.uremanager("btrfs-plan")==0,"Exact controller review was unavailable while original worker ran");
            const auto control_review=ure::parse_json(value("ure_output")); check(control_review["owner_job_id"]==id && control_review["journal"]==journal.string(),"Controller review retargeted the mutable root or journal");
            DataManager::SetValue("ure_manage_hash",std::string(64,'0')); const auto calls=control_calls.load();
            check(action.uremanager("btrfs-execute")==1 && calls==control_calls,"Wrong controller confirmation reached its ioctl");
            DataManager::SetValue("ure_manage_hash",review["plan_sha256"].asString()); check(action.uremanager("btrfs-execute")==0,"Exact confirmed controller was refused");
            collect_all(action); check(!ure::fs::exists(work/"unopened-root") && !ure::fs::exists(work/"unopened-parent"),"Controller opened changed selections");
            const auto controlled=ure::parse_json(value("ure_job_control_result"));
            check(controlled["output"]["state"]=="CONTROL_SENT" && controlled["output"]["action"]==selected && controlled["output"]["controlled_job_id"]==id,
                "Wrong controller acknowledgement or lost original job identity");
            if(scenario==1) {
                check(ure::parse_json(ure::Root(journal).read("state.json"))["state"]=="PAUSED" && ure::operation_lease_status()["retained_owner"]==true,
                    "Paused balance discarded its durable owner");
                { ure::LegacyLifecycleGuard reboot("reboot"); check(!reboot.active(),"Paused maintenance permitted reboot after worker return"); }
                DataManager::SetValue("ure_btrfs_action","balance-cancel"); check(action.uremanager("btrfs-plan")==0 && action.uremanager("btrfs-execute")==0,"Paused exact balance controller became unreachable");
                collect_all(action);
            }
            if(scenario!=0) {
                check(action.uremanager("job-status")==0 && value("ure_job_backend_cleanable")=="1","Failed/paused backend did not retain an explicit cleanup path");
                check(action.uremanager("job-backend-cleanup")==0,"Cannot queue exact inactive-maintenance verification"); collect_all(action);
            }
            const auto final=ure::parse_json(ure::Root(journal).read("state.json"));
            check(final["state"]=="CANCELLED_SAFE" && final["kernel_maintenance_inactive"]==true && ure::operation_lease_status()["state"]=="IDLE" &&
                ure::runtime_active_jobs()==0,"Cancelled Btrfs cleanup did not independently retire exact ownership");
            check(ure::Root(root).read("marker")=="original\n","Btrfs control modified private root marker bytes");
        }
        ure_gui_shutdown_jobs(); check(management_foreign_reads==0 && management_foreign_writes==0,"Btrfs control worker accessed GUI state");
        ure::fs::remove_all(work); std::cout<<"PASS actual GUI/Btrfs control: captured root/hash/journal, active scrub cancel, retained pause/cancel, worker exception, explicit inactive verification and lifecycle refusal; exact-FD host ioctl stand-ins only\n"; return 0;
    } catch(const std::exception& error) {
        cancel=true; running=false; ure_gui_shutdown_jobs(); std::cerr<<error.what()<<"\nPrivate GUI Btrfs fixture retained: "<<work<<'\n'; return 1;
    }
}
