// SPDX-License-Identifier: Apache-2.0
#include "rescue_resources.hpp"
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <fcntl.h>
#include <linux/magic.h>
#include <poll.h>
#include <sched.h>
#include <set>
#include <sstream>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <sys/statfs.h>
#include <unistd.h>

namespace ure {
namespace {
constexpr std::uint64_t mib=1024*1024;
std::string trim(const std::string& value) {
    const auto first=value.find_first_not_of(" \t\r\n"),last=value.find_last_not_of(" \t\r\n");
    return first==value.npos ? "" : value.substr(first,last-first+1);
}
std::uint64_t number(const std::string& text) {
    const auto value=trim(text); std::uint64_t result=0;
    const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    require(!value.empty() && parsed.ec==std::errc{} && parsed.ptr==value.data()+value.size(),
        "rescue-resource-unavailable","Malformed kernel resource counter");
    return result;
}
std::set<std::string> words(const std::string& text) {
    std::set<std::string> result; std::istringstream input(text); std::string word;
    while(input>>word)require(result.insert(word).second && result.size()<=64,
        "rescue-resource-unavailable","Malformed kernel controller list");
    return result;
}
Value counters(const std::string& text) {
    Value result; std::istringstream input(text); std::string key,value;
    unsigned count=0;
    while(input>>key) {
        require(static_cast<bool>(input>>value) && ++count<=64 && !result.isMember(key),
            "rescue-resource-unavailable","Malformed kernel resource events");
        result[key]=Json::UInt64(number(value));
    }
    return result;
}
void control(const Root& group,const std::string& name,const std::string& text) {
    auto fd=group.open(name,O_WRONLY); const auto data=text+"\n"; ssize_t written;
    do { written=::write(fd.get(),data.data(),data.size()); } while(written<0 && errno==EINTR);
    require(written==static_cast<ssize_t>(data.size()),"rescue-resource-unavailable","Cannot apply complete kernel resource control");
}
void headroom(const Root& cgroups,const Value& policy) {
    Root proc("/proc"); struct statfs type{};
    require(::fstatfs(proc.fd(),&type)==0 && type.f_type==PROC_SUPER_MAGIC,
        "rescue-resource-unavailable","Resource admission needs the kernel proc filesystem");
    std::istringstream info(proc.read("meminfo",64*1024)); std::string line; std::uint64_t available=0; bool found=false;
    while(std::getline(info,line))if(line.starts_with("MemAvailable:")) {
        require(!found,"rescue-resource-unavailable","Duplicate kernel memory availability");
        std::istringstream row(line.substr(13)); std::string amount,unit,extra;
        require(static_cast<bool>(row>>amount>>unit) && unit=="kB" && !(row>>extra),
            "rescue-resource-unavailable","Malformed kernel memory availability");
        const auto kib=number(amount); require(kib<=UINT64_MAX/1024,"rescue-resource-unavailable","Kernel memory availability overflow");
        available=kib*1024; found=true;
    }
    require(found,"rescue-resource-unavailable","Kernel memory availability is absent");
    std::string selected;
    // Do not relax Root's no-symlink policy for procfs's magic self alias.
    std::istringstream memberships(proc.read(std::to_string(::getpid())+"/cgroup",8192));
    while(std::getline(memberships,line))if(line.starts_with("0::")) {
        require(selected.empty(),"rescue-resource-unavailable","Duplicate unified cgroup membership"); selected=line.substr(3);
    }
    require(!selected.empty() && selected[0]=='/' && selected.size()<=4096,
        "rescue-resource-unavailable","Unified cgroup membership is unavailable");
    if(selected!="/")components(selected.substr(1));
    auto admit_ancestors=[&](std::string relative) {
        for(unsigned depth=0;!relative.empty();++depth) {
            require(depth<64,"rescue-resource-unavailable","Resource ancestor depth exceeds its bound");
            const auto maximum=trim(cgroups.read(relative+"/memory.max",128));
            const auto used=number(cgroups.read(relative+"/memory.current",128));
            if(maximum!="max") { const auto limit=number(maximum); available=std::min(available,used>=limit ? 0 : limit-used); }
            const auto slash=relative.rfind('/'); relative=slash==relative.npos ? "" : relative.substr(0,slash);
        }
    };
    if(selected!="/")admit_ancestors(selected.substr(1));
    if(cgroups.exists("ure-rescue"))admit_ancestors("ure-rescue");
    // A cgroup namespace may expose a delegated subtree as its mount root.
    // Its own memory limit remains an ancestor even though its path is '/'.
    if(cgroups.exists("memory.max")) {
        const auto maximum=trim(cgroups.read("memory.max",128)),used_text=cgroups.read("memory.current",128);
        const auto used=number(used_text);
        if(maximum!="max") { const auto limit=number(maximum); available=std::min(available,used>=limit ? 0 : limit-used); }
    }
    require(available>=policy["memory_max_bytes"].asUInt64()+policy["gui_reserve_bytes"].asUInt64(),
        "rescue-resource-headroom","Keep the reviewed GUI/kernel memory reserve outside the complete rescue envelope");
}
class KernelResources final:public RescueResources {
    std::unique_ptr<Root> cgroups_=std::make_unique<Root>("/sys/fs/cgroup");
    std::unique_ptr<Root> manager_,group_;
    Value policy_;
    std::string name_;
    bool created_=false,released_=false,attached_=false,configured_=false;
    Value final_;
    void verify() const {
        require(number(group_->read("memory.max",128))==policy_["memory_max_bytes"].asUInt64() &&
            number(group_->read("memory.high",128))==policy_["memory_high_bytes"].asUInt64() &&
            number(group_->read("memory.swap.max",128))==0 && number(group_->read("memory.oom.group",128))==1 &&
            number(group_->read("pids.max",128))==policy_["pids_max"].asUInt64(),
            "rescue-resource-unavailable","Kernel memory, swap, OOM or process controls differ from review");
        std::istringstream cpu(group_->read("cpu.max",128)); std::string quota,period,extra;
        require(static_cast<bool>(cpu>>quota>>period) && !(cpu>>extra) &&
            number(quota)==policy_["cpu_quota_us"].asUInt64() && number(period)==policy_["cpu_period_us"].asUInt64(),
            "rescue-resource-unavailable","Kernel CPU quota differs from review");
    }
    bool empty() const {
        const auto events=counters(group_->read("cgroup.events",4096));
        require(events["populated"].isUInt64() && events["populated"].asUInt64()<=1,
            "rescue-resource-unavailable","Kernel cgroup population is unavailable");
        return events["populated"].asUInt64()==0 && trim(group_->read("cgroup.procs",4096)).empty();
    }
public:
    KernelResources(const Value& policy,const std::string& id):policy_(policy),name_("job-"+id) {}
    void configure() {
        struct statfs type{};
        require(::fstatfs(cgroups_->fd(),&type)==0 && type.f_type==CGROUP2_SUPER_MAGIC,
            "rescue-resource-unavailable","Aggregate rescue enforcement requires a real unified cgroup mount");
        const auto enabled=words(cgroups_->read("cgroup.subtree_control",4096));
        for(const auto* key:{"memory","pids","cpu"})require(enabled.contains(key),
            "rescue-resource-unavailable","Required controller is not delegated by the cgroup mount root");
        headroom(*cgroups_,policy_);
        if(!cgroups_->exists("ure-rescue"))require(::mkdirat(cgroups_->fd(),"ure-rescue",0700)==0,
            "rescue-resource-unavailable","Cannot create the private recovery resource manager");
        const auto st=cgroups_->stat("ure-rescue");
        require(S_ISDIR(st.st_mode) && st.st_uid==::geteuid() && (st.st_mode&07777)==0700,
            "rescue-resource-unavailable","Resource manager must be a private owned kernel directory");
        manager_=std::make_unique<Root>("/sys/fs/cgroup/ure-rescue");
        require(trim(manager_->read("cgroup.type",128))=="domain" && trim(manager_->read("cgroup.procs",4096)).empty(),
            "rescue-resource-unavailable","Resource manager must be an empty domain without internal processes");
        const auto supported=words(manager_->read("cgroup.controllers",4096));
        for(const auto* key:{"memory","pids","cpu"})require(supported.contains(key),
            "rescue-resource-unavailable","Resource manager lacks a required delegated controller");
        control(*manager_,"cgroup.subtree_control","+memory +pids +cpu");
        require(::mkdirat(manager_->fd(),name_.c_str(),0700)==0,"rescue-resource-unavailable","Cannot create a fresh bounded rescue cgroup");
        created_=true; group_=std::make_unique<Root>(fs::path("/sys/fs/cgroup/ure-rescue")/name_);
        require(empty(),"rescue-resource-unavailable","Fresh resource group already contains a workload");
        control(*group_,"memory.max",std::to_string(policy_["memory_max_bytes"].asUInt64()));
        control(*group_,"memory.high",std::to_string(policy_["memory_high_bytes"].asUInt64()));
        control(*group_,"memory.swap.max","0"); control(*group_,"memory.oom.group","1");
        control(*group_,"pids.max",std::to_string(policy_["pids_max"].asUInt64()));
        control(*group_,"cpu.max",std::to_string(policy_["cpu_quota_us"].asUInt64())+" "+std::to_string(policy_["cpu_period_us"].asUInt64()));
        auto kill=group_->open("cgroup.kill",O_WRONLY); (void)kill;
        verify(); headroom(*cgroups_,policy_); configured_=true;
    }
    void attach_worker(pid_t pid) override {
        require(pid>0 && !attached_,"rescue-resource-unavailable","Resource worker attachment must be fresh and exact");
        verify(); control(*group_,"cgroup.procs",std::to_string(pid));
        require(trim(group_->read("cgroup.procs",4096))==std::to_string(pid),
            "rescue-resource-unavailable","Kernel did not attach the blocked worker exclusively to the reviewed group");
        verify(); attached_=true;
    }
    void close_in_child() noexcept override {
        // PID-namespace init must not retain writable controller descriptors
        // that its installed payload could reopen through /proc/1/fd.
        group_.reset(); manager_.reset(); cgroups_.reset(); released_=true;
    }
    Value observation() const override {
        if(released_)return final_;
        verify(); Value out; out["backend"]="kernel-cgroup-v2"; out["aggregate_limits_enforced"]=true;
        out["worker_attachment_verified"]=attached_; out["group_name"]=name_; out["limits"]=policy_;
        out["memory_current_bytes"]=Json::UInt64(number(group_->read("memory.current",128)));
        out["memory_peak_available"]=group_->exists("memory.peak");
        if(out["memory_peak_available"]==true)out["memory_peak_bytes"]=Json::UInt64(number(group_->read("memory.peak",128)));
        out["pids_current"]=Json::UInt64(number(group_->read("pids.current",128)));
        out["memory_events"]=counters(group_->read("memory.events",4096));
        out["pids_events"]=counters(group_->read("pids.events",4096));
        out["cpu_stat"]=counters(group_->read("cpu.stat",4096));
        out["group_empty_verified"]=empty(); return out;
    }
    void kill_all() noexcept override {
        if(!group_ || released_)return;
        try { control(*group_,"cgroup.kill","1"); } catch(...) {}
    }
    bool finish() noexcept override {
        if(released_)return true;
        try {
            if(!group_) {
                if(!created_)return true;
                if(!manager_ || ::unlinkat(manager_->fd(),name_.c_str(),AT_REMOVEDIR)!=0)return false;
                released_=true; return true;
            }
            if(!empty())kill_all();
            const auto deadline=monotonic_ms()+5000;
            while(!empty() && monotonic_ms()<deadline) { pollfd none{}; ::poll(&none,0,20); }
            if(!empty())return false;
            if(configured_)final_=observation();
            if(::unlinkat(manager_->fd(),name_.c_str(),AT_REMOVEDIR)!=0)return false;
            group_.reset();
            released_=true; final_["group_removed_verified"]=true; return true;
        } catch(...) { return false; }
    }
    ~KernelResources() override { if(!released_) { kill_all(); static_cast<void>(finish()); } }
};
}
Value rescue_resource_policy(const Value& request) {
    const auto settings=request.get("resources",Value(Json::objectValue));
    require(settings.isObject(),"invalid-rescue-resources","Resource choices must be an object");
    for(const auto& key:settings.getMemberNames())require(key=="memory_mib" || key=="jobs" || key=="pids",
        "invalid-rescue-resources","Unknown rescue resource choice");
    auto choice=[&](const char* key,unsigned fallback,unsigned minimum,unsigned maximum) {
        const auto value=settings.get(key,fallback);
        require(value.isUInt() && value.asUInt()>=minimum && value.asUInt()<=maximum,
            "invalid-rescue-resources","Rescue memory/job/process choice is outside its reviewed bounds"); return value.asUInt(); };
    const auto memory=choice("memory_mib",1024,128,2048),jobs=choice("jobs",2,1,4),pids=choice("pids",128,16,256);
    Value out; out["schema"]=1; out["backend"]="kernel-cgroup-v2"; out["aggregate_required"]=true;
    out["memory_max_bytes"]=Json::UInt64(memory*mib); out["memory_high_bytes"]=Json::UInt64(memory*mib*3/4);
    out["memory_swap_max_bytes"]=Json::UInt64(0); out["oom_group_required"]=true; out["pids_max"]=pids;
    out["cpu_quota_us"]=jobs*100000; out["cpu_period_us"]=100000; out["job_limit"]=jobs;
    out["tmpfs_bytes"]=Json::UInt64(memory*mib/4); out["gui_reserve_bytes"]=Json::UInt64(512*mib);
    out["nofile_hard_limit"]=1024; out["core_hard_limit"]=0; out["rlimit_nproc_fallback_accepted"]=false;
    out["proc_mount_readonly_required"]=true; out["oom_adjustment_nonnegative_required"]=true;
    out["supervisor_nondumpable_required"]=true;
    out["unbounded_fallback_accepted"]=false; return out;
}
Value rescue_resource_capabilities() {
    Value out; out["schema"]=1; out["source_implemented"]=true; out["backend"]="kernel-cgroup-v2";
    out["controller_path_configurable"]=false; out["unbounded_fallback_available"]=false;
    out["admission_required_before_worker_start"]=true; out["worker_attachment_required_before_namespace"]=true;
    out["physical_device_accepted"]=false; out["default_limits"]=rescue_resource_policy(Value(Json::objectValue));
    try {
        Root cgroups("/sys/fs/cgroup"); struct statfs type{};
        require(::fstatfs(cgroups.fd(),&type)==0 && type.f_type==CGROUP2_SUPER_MAGIC,"rescue-resource-unavailable","Unified cgroup mount is absent");
        const auto enabled=words(cgroups.read("cgroup.subtree_control",4096));
        for(const auto* key:{"memory","pids","cpu"})require(enabled.contains(key),"rescue-resource-unavailable","A required controller is not delegated");
        headroom(cgroups,out["default_limits"]);
        require(::faccessat(cgroups.fd(),".",W_OK,AT_EACCESS)==0 || (cgroups.exists("ure-rescue") &&
            ::faccessat(cgroups.fd(),"ure-rescue",W_OK,AT_EACCESS)==0),"rescue-resource-unavailable","Private cgroup manager is not writable");
        out["readonly_admission_probe_passed"]=true;
    } catch(const Error& error) { out["readonly_admission_probe_passed"]=false; out["reason_code"]=error.code; out["reason"]=error.what(); }
    out["live_enforcement_verified"]=false; return out;
}
void rescue_process_limits(const Value& policy) {
    const rlimit core{0,0},files{1024,1024}; sched_param scheduling{};
    require(::setrlimit(RLIMIT_CORE,&core)==0 && ::setrlimit(RLIMIT_NOFILE,&files)==0 &&
        ::sched_setscheduler(0,SCHED_OTHER,&scheduling)==0 && ::setpriority(PRIO_PROCESS,0,5)==0,
        "rescue-resource-unavailable","Cannot apply supplemental process and fair scheduling controls");
    require(policy["aggregate_required"]==true && policy["unbounded_fallback_accepted"]==false,
        "rescue-resource-unavailable","Supplemental process limits do not replace aggregate enforcement");
    // A protected recovery GUI may use -1000. The installed workload must not
    // inherit that OOM exemption, including namespace init and all descendants.
    Root proc("/proc"); Root process(proc.open(std::to_string(::getpid()),O_RDONLY|O_DIRECTORY));
    const auto text=trim(process.read("oom_score_adj",128)); int score=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),score);
    require(!text.empty() && parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size() && score>=-1000 && score<=1000,
        "rescue-resource-unavailable","Cannot inspect workload OOM eligibility");
    // Preserve a positive inherited score: lowering a browser's protected
    // minimum is unnecessary and can be denied to an unprivileged host UID.
    if(score<0)control(process,"oom_score_adj","0");
    require(trim(process.read("oom_score_adj",128))==(score<0 ? "0" : text) && ::prctl(PR_SET_DUMPABLE,0)==0,
        "rescue-resource-unavailable","Cannot isolate supervisor descriptors and restore workload OOM eligibility");
}
extern "C" RescueResources* ure_create_rescue_resources(const Value* policy,const char* operation_id) {
    require(policy && operation_id && identifier(operation_id) && std::string_view(operation_id).size()==32,
        "invalid-rescue-resources","Resource group must bind the exact rescue operation");
    try {
        auto resources=std::make_unique<KernelResources>(*policy,operation_id);
        resources->configure(); return resources.release();
    } catch(const Error& error) {
        if(error.code=="rescue-resource-headroom" || error.code=="rescue-resource-unavailable")throw;
        throw Error("rescue-resource-unavailable","Kernel aggregate resource admission failed: "+std::string(error.what()));
    }
}
}
