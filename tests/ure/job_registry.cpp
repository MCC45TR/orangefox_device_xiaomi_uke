// SPDX-License-Identifier: Apache-2.0
// Runtime locks on private reference-host files; no mount/reboot effects.
#include "job_registry.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
namespace {
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
constexpr const char* first_id="11111111111111111111111111111111";
constexpr const char* second_id="22222222222222222222222222222222";
void child(const char* executable,const char* mode) {
    pid_t process=-1; char* arguments[]={const_cast<char*>(executable),const_cast<char*>(mode),nullptr};
    check(::posix_spawn(&process,executable,nullptr,nullptr,arguments,environ)==0,"Cannot execute independent registry contender");
    int status=0; check(::waitpid(process,&status,0)==process && WIFEXITED(status) && WEXITSTATUS(status)==0,"Independent registry contender failed");
}
void bytes(const std::filesystem::path& path,const char* content) {
    std::ofstream file(path); file<<content; check(file.good(),"Cannot create private unsafe-registry fixture"); file.close();
    check(::chmod(path.c_str(),0600)==0,"Cannot set private registry fixture mode");
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2) {
            const auto mode=std::string_view(argv[1]);
            auto selected=ure::RuntimeActivityLease::acquire(mode=="--blocked-lifecycle" ? nullptr : first_id,mode=="--blocked-lifecycle");
            if(mode=="--blocked-lifecycle" || mode=="--blocked-job")return !selected.acquired() && std::strcmp(selected.error(),"gui-lifecycle-busy")==0 ? 0 : 1;
            if(mode=="--unsafe")return !selected.acquired() &&
                (std::strcmp(selected.error(),"unsafe-gui-registry")==0 || std::strcmp(selected.error(),"gui-registry-unavailable")==0) ? 0 : 1;
            return 1;
        }
        const auto* configured=::getenv("URE_GUI_JOB_REGISTRY"); check(configured!=nullptr,"Missing private host registry configuration");
        const std::filesystem::path directory(configured),parent=directory.parent_path();
        {
            auto first=ure::RuntimeActivityLease::acquire(first_id),second=ure::RuntimeActivityLease::acquire(second_id);
            check(first.valid() && second.valid() && ure::runtime_active_jobs()==2,"Two owned jobs were not registered");
            auto duplicate=ure::RuntimeActivityLease::acquire(first_id);
            check(!duplicate.acquired() && std::strcmp(duplicate.error(),"gui-registry-job-mismatch")==0,"A duplicate job identity was admitted");
            auto reboot=ure::RuntimeActivityLease::acquire(nullptr,true);
            check(!reboot.acquired() && std::strcmp(reboot.error(),"gui-lifecycle-busy")==0,"An active job allowed lifecycle admission");
            child(argv[0],"--blocked-lifecycle");
        }
        check(ure::runtime_active_jobs()==0,"Finished activities remained registered");
        {
            auto transition=ure::RuntimeActivityLease::acquire(nullptr,true); check(transition.valid(),"Idle lifecycle was refused");
            auto job=ure::RuntimeActivityLease::acquire(first_id); check(!job.acquired(),"A worker started during a held lifecycle effect");
            child(argv[0],"--blocked-job");
            auto moved=std::move(transition); check(moved.valid() && !transition.acquired(),"Move lost or duplicated runtime ownership");
        }
        {
            std::array<ure::RuntimeActivityLease,64> held;
            for(std::size_t i=0;i<held.size();++i) {
                std::array<char,33> id{}; check(::snprintf(id.data(),id.size(),"%032zx",i+1)==32,"Cannot format exact fixture identity");
                held[i]=ure::RuntimeActivityLease::acquire(id.data()); check(held[i].valid(),"Bounded registry refused an available slot");
            }
            auto excess=ure::RuntimeActivityLease::acquire(first_id);
            check(!excess.acquired() && std::strcmp(excess.error(),"gui-registry-job-limit")==0 && ure::runtime_active_jobs()==64,"Registry exceeded its entry budget");
        }
        check(ure::runtime_active_jobs()==0,"Registry budget refusal leaked a slot");
        {
            auto job=ure::RuntimeActivityLease::acquire(first_id); check(job.valid(),"Cannot hold path-replacement oracle");
            const auto alternate=(parent/"unopened-alternate").string();
            check(::setenv("URE_GUI_JOB_REGISTRY",alternate.c_str(),1)==0,"Cannot change host test configuration");
            auto switched=ure::RuntimeActivityLease::acquire(second_id);
            check(!switched.acquired() && !std::filesystem::exists(alternate) && !job.valid(),"Domain switching touched a new path or reused old authority");
            check(::setenv("URE_GUI_JOB_REGISTRY",directory.c_str(),1)==0 && job.valid(),"Cannot restore the exact host registry configuration");
            const auto saved=parent/"saved-gui-registry"; std::filesystem::rename(directory,saved);
            check(::mkdir(directory.c_str(),0700)==0,"Cannot create replaced runtime directory");
            auto replacement=ure::RuntimeActivityLease::acquire(nullptr,true);
            check(!replacement.acquired() && std::strcmp(replacement.error(),"gui-registry-domain-changed")==0 && !job.valid(),"Replacement directory borrowed old runtime authority");
            std::filesystem::remove_all(directory); std::filesystem::rename(saved,directory); check(job.valid(),"Restored exact registry identity did not validate");
            std::filesystem::rename(directory/"activity.lock",directory/"saved.lock"); bytes(directory/"activity.lock","");
            auto new_lock=ure::RuntimeActivityLease::acquire(second_id);
            check(!new_lock.acquired() && !job.valid(),"Replacement lock borrowed old runtime authority");
            std::filesystem::remove(directory/"activity.lock"); std::filesystem::rename(directory/"saved.lock",directory/"activity.lock"); check(job.valid(),"Cannot restore exact activity lock");
        }
        for(const auto* fault:{"directory-mode","file-mode","symlink","hardlink","content","fifo"}) {
            const auto unsafe=parent/fault; check(::mkdir(unsafe.c_str(),0700)==0,"Cannot create unsafe fixture directory");
            const auto file=unsafe/"activity.lock"; bytes(file,"");
            if(std::strcmp(fault,"directory-mode")==0)check(::chmod(unsafe.c_str(),0755)==0,"Cannot create directory mode refusal");
            if(std::strcmp(fault,"file-mode")==0)check(::chmod(file.c_str(),0644)==0,"Cannot create file mode refusal");
            if(std::strcmp(fault,"content")==0)bytes(file,"unexpected lock payload");
            if(std::strcmp(fault,"hardlink")==0)check(::link(file.c_str(),(unsafe/"alias").c_str())==0,"Cannot create hardlink refusal");
            if(std::strcmp(fault,"symlink")==0) { std::filesystem::rename(file,unsafe/"actual"); std::filesystem::create_symlink("actual",file); }
            if(std::strcmp(fault,"fifo")==0) { std::filesystem::remove(file); check(::mkfifo(file.c_str(),0600)==0,"Cannot create FIFO refusal"); }
            check(::setenv("URE_GUI_JOB_REGISTRY",unsafe.c_str(),1)==0,"Cannot select unsafe child fixture"); child(argv[0],"--unsafe");
        }
        check(::setenv("URE_GUI_JOB_REGISTRY",directory.c_str(),1)==0 && ure::runtime_active_jobs()==0,"Unsafe child fixture changed parent ownership");
        std::cout<<"PASS runtime registry: independent-process exclusion, held lifecycle effects, exact moves/identities, bounded slots, path/lock replacement and private-file refusals; no mount/reboot effects\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
