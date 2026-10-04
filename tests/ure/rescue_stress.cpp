// SPDX-License-Identifier: Apache-2.0
// Real cgroup-v2 controls, only in tests/with-rescue-cgroup.sh's fresh host scope.
// No installed root, real partition, mount or physical device is used.
#include "rescue_resources.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
namespace {
void check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
enum class Load { Memory,Pids,Cpu,Termination };
void busy() { volatile unsigned value=1; for(;;)value=value*1664525U+1013904223U; }
[[noreturn]] void payload(Load load,int output) {
    if(load==Load::Memory) {
        constexpr std::size_t bytes=256ULL*1024*1024;
        auto* data=static_cast<volatile unsigned char*>(::mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
        if(data==MAP_FAILED)::_exit(80);
        // Populate actual writable anonymous pages. A kernel may throttle
        // memory.high before reaching memory.max; record that distinction
        // rather than treating a supervisor deadline as a local OOM kill.
        if(::madvise(const_cast<unsigned char*>(data),bytes,MADV_POPULATE_WRITE)!=0)::_exit(87);
        for(std::size_t offset=0;offset<bytes;offset+=4096)data[offset]=1;
        ::_exit(81); // Reaching the end would refute the 128 MiB envelope.
    }
    if(load==Load::Pids) {
        std::array<pid_t,64> children{}; unsigned count=0; bool limited=false;
        while(count<children.size()) {
            const auto child=::fork();
            if(child<0) { limited=errno==EAGAIN; break; }
            if(child==0)for(;;)::pause();
            children[count++]=child;
        }
        const char result=limited && count>=4 ? 'P' : 'X'; static_cast<void>(::write(output,&result,1));
        for(unsigned i=0;i<count;++i)static_cast<void>(::kill(children[i],SIGKILL));
        while(::waitpid(-1,nullptr,0)>0 || errno==EINTR) {}
        ::_exit(result=='P' ? 0 : 82);
    }
    if(load==Load::Cpu) {
        const auto child=::fork(); if(child<0)::_exit(83);
        if(child==0)busy();
        busy();
    }
    const auto child=::fork(); if(child<0)::_exit(84);
    if(child==0)for(;;)::pause();
    static_cast<void>(::write(output,"T",1)); for(;;)::pause();
}
}
int main() {
    try {
        // Refuse a direct accidental invocation in the desktop's real mount.
        ure::Root cgroups("/sys/fs/cgroup"),proc("/proc");
        const auto membership=proc.read(std::to_string(::getpid())+"/cgroup",8192);
        check(membership=="0::/supervisor\n" && cgroups.exists("memory.max") &&
            cgroups.read("memory.max",128)=="1073741824\n","Stress must run inside its fresh delegated 1 GiB host scope");
        ure::Value request; request["resources"]["memory_mib"]=128; request["resources"]["jobs"]=1; request["resources"]["pids"]=16;
        const auto policy=ure::rescue_resource_policy(request);
        for(const auto load:{Load::Memory,Load::Pids,Load::Cpu,Load::Termination}) {
            const auto id=ure::operation_id();
            std::unique_ptr<ure::RescueResources> resources(ure::ure_create_rescue_resources(&policy,id.c_str()));
            int start[2]{},messages[2]{}; check(::pipe2(start,O_CLOEXEC)==0 && ::pipe2(messages,O_CLOEXEC|O_NONBLOCK)==0,"Cannot create stress handoff");
            ure::Fd send(start[1]),receive(start[0]),result(messages[0]),publish(messages[1]);
            const auto worker=::fork(); check(worker>=0,"Cannot start bounded stress worker");
            if(worker==0) {
                resources->close_in_child(); send=ure::Fd(); result=ure::Fd();
                char armed=0; if(::read(receive.get(),&armed,1)!=1 || armed!='R')::_exit(85);
                receive=ure::Fd();
                try { ure::rescue_process_limits(policy); payload(load,publish.get()); }
                catch(const std::exception& error) { std::cerr<<"Bounded worker refused: "<<error.what()<<" errno="<<errno<<'\n'; ::_exit(86); }
            }
            receive=ure::Fd(); publish=ure::Fd(); resources->attach_worker(worker);
            check(::write(send.get(),"R",1)==1,"Cannot release real bounded worker"); send=ure::Fd();
            int status=0; pid_t collected=0; const auto started=ure::monotonic_ms(); auto previous=started;
            std::uint64_t max_gap=0,ticks=0; bool killed=false;
            while((collected=::waitpid(worker,&status,WNOHANG))==0) {
                const auto now=ure::monotonic_ms(); max_gap=std::max(max_gap,now-previous); previous=now; ++ticks;
                if((load==Load::Cpu && now-started>=2000) || (load==Load::Termination && now-started>=200) || now-started>=5000) {
                    resources->kill_all(); killed=true;
                }
                ::poll(nullptr,0,10);
            }
            check(collected==worker,"Cannot reap stress worker"); auto evidence=resources->observation();
            std::cout<<"Observed host load="<<static_cast<int>(load)<<" wait_status="<<status<<" deadline_kill="<<killed<<
                " elapsed_ms="<<ure::monotonic_ms()-started<<" evidence="<<ure::json(evidence)<<std::endl;
            if(load==Load::Memory) {
                const bool local_oom=!killed && WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL && evidence["memory_events"]["oom_kill"].asUInt64()>0;
                const bool throttled=killed && ticks>=40 && max_gap<500 && evidence["memory_events"]["high"].asUInt64()>0;
                check((local_oom || throttled) && evidence["memory_peak_available"]==true &&
                    evidence["memory_peak_bytes"].asUInt64()<=policy["memory_max_bytes"].asUInt64()+4*1024*1024,
                    "Allocation completed, escaped its memory budget or stalled the outside supervisor");
                std::cout<<"Memory outcome="<<(local_oom ? "local-oom-kill" : "memory-high-throttled-and-deadline-terminated")<<'\n';
            }
            if(load==Load::Pids) {
                char message=0; check(WIFEXITED(status) && WEXITSTATUS(status)==0 && ::read(result.get(),&message,1)==1 && message=='P' &&
                    evidence["pids_events"]["max"].asUInt64()>0,"Kernel PID envelope did not reject bounded forks");
            }
            if(load==Load::Cpu)check(killed && ticks>=40 && max_gap<500 && evidence["cpu_stat"]["nr_throttled"].asUInt64()>0,
                "CPU quota or independent supervisor heartbeat was not observed");
            if(load==Load::Termination)check(killed && WIFSIGNALED(status),"Whole-group termination did not collect worker and descendant");
            check(resources->finish(),"Kernel stress group did not close"); evidence=resources->observation();
            check(evidence["aggregate_limits_enforced"]==true && evidence["group_empty_verified"]==true &&
                evidence["group_removed_verified"]==true && !cgroups.exists("ure-rescue/job-"+id),"Real resource group removal was not verified");
            std::cout<<"PASS real delegated host cgroup load="<<static_cast<int>(load)<<" elapsed_ms="<<ure::monotonic_ms()-started<<
                " supervisor_ticks="<<ticks<<" max_gap_ms="<<max_gap<<" evidence="<<ure::json(evidence)<<'\n';
        }
        request["resources"]["memory_mib"]=2048; const auto excessive=ure::rescue_resource_policy(request); const auto id=ure::operation_id(); bool refused=false;
        try { std::unique_ptr<ure::RescueResources> resources(ure::ure_create_rescue_resources(&excessive,id.c_str())); }
        catch(const ure::Error& error) { refused=error.code=="rescue-resource-headroom"; }
        check(refused && !cgroups.exists("ure-rescue/job-"+id),"Ancestor/GUI reserve admission created an oversized workload");
        std::cout<<"PASS real ancestor headroom refusal; host scope only, GUI rendering, target kernel and tablet acceptance remain separate\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
