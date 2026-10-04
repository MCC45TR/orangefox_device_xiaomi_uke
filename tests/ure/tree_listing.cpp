// SPDX-License-Identifier: Apache-2.0
#include "tree_listing.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <sys/resource.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fault { int mode=0, barrier=-1; bool fallback=false; }
extern "C" ssize_t __real_write(int,const void*,size_t);
extern "C" int __real_openat(int,const char*,int,...);
extern "C" int __real_fstatvfs(int,struct statvfs*);
extern "C" int __wrap_openat(int directory,const char* path,int flags,...) {
    mode_t mode=0;
    if((flags&O_CREAT) || (flags&O_TMPFILE)==O_TMPFILE) { va_list args; va_start(args,flags); mode=static_cast<mode_t>(va_arg(args,int)); va_end(args); }
    if(fault::fallback && (flags&O_TMPFILE)==O_TMPFILE) { errno=EOPNOTSUPP; return -1; }
    return __real_openat(directory,path,flags,mode);
}
extern "C" int __wrap_fstatvfs(int fd,struct statvfs* value) {
    const int result=__real_fstatvfs(fd,value);
    if(result==0 && fault::mode==4) { value->f_bavail=0; fault::mode=0; }
    return result;
}
extern "C" ssize_t __wrap_write(int fd,const void* bytes,size_t count) {
    struct stat st{};
    if(fault::mode && ::fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_nlink==0) {
        const int mode=fault::mode; fault::mode=0;
        if(mode==1) { errno=EINTR; return -1; }
        if(mode==2)return __real_write(fd,bytes,count>1 ? count/2 : count);
        if(mode==3) { errno=ENOSPC; return -1; }
        if(mode==5) {
            const auto result=__real_write(fd,bytes,count);
            if(result<=0 || ::write(fault::barrier,"R",1)!=1)::_exit(4);
            for(;;)::pause();
        }
    }
    return __real_write(fd,bytes,count);
}
namespace {
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
template<class F> void reject(F action,const char* code) {
    try { action(); } catch(const ure::Error& error) { check(error.code==code,"Unexpected enumeration refusal"); return; }
    throw std::runtime_error("Expected enumeration refusal");
}
unsigned descriptors() {
    DIR* directory=::opendir("/proc/self/fd"); check(directory,"Cannot inspect fixture descriptors"); unsigned count=0;
    while(const auto* e=::readdir(directory))if(std::strcmp(e->d_name,".") && std::strcmp(e->d_name,".."))++count;
    ::closedir(directory); return count;
}
void numbered_name(unsigned value,std::string& result) {
    result.assign(255,'x'); result[240]='-'; const auto number=std::to_string(value);
    result.replace(241,6-number.size(),6-number.size(),'0'); result.replace(247-number.size(),number.size(),number); result[247]='-';
}
void create(int directory,const std::string& name) {
    ure::Fd fd(::openat(directory,name.c_str(),O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW|O_CLOEXEC,0600)); check(fd.get()>=0,"Cannot create synthetic name");
}
void scan(int source,int store,unsigned count) {
    ure::TreeListingBudget budget; std::string actual,expected; actual.reserve(255); expected.reserve(255);
    struct rusage before{},after{}; check(::getrusage(RUSAGE_SELF,&before)==0,"Cannot inspect initial RSS");
    const auto started=ure::monotonic_ms();
    {
        ure::SortedTreeDirectory listing(source,store,budget,true);
        for(unsigned index=0;index<count;++index) { numbered_name(index,expected); check(listing.next(actual) && actual==expected,"Sorted maximum-length namespace differs"); }
        check(!listing.next(actual),"Unexpected additional child"); listing.rewind();
        numbered_name(0,expected); check(listing.next(actual) && actual==expected,"Enumeration rewind lost the first child");
    }
    check(::getrusage(RUSAGE_SELF,&after)==0,"Cannot inspect final RSS");
    check(budget.memory==0 && budget.scratch==0 && budget.admitted_entries==count+1,"Enumeration leaked resources or lost its admitted count");
    check(after.ru_maxrss-before.ru_maxrss<32768,"Isolated enumeration RSS grew by at least 32 MiB");
    std::cout<<"Wide enumeration: children="<<count<<", name_bytes=255, accounted_peak_bytes="<<budget.peak_memory
        <<", scratch_peak_bytes="<<budget.peak_scratch<<", rss_before_kib="<<before.ru_maxrss<<", rss_peak_kib="<<after.ru_maxrss
        <<", elapsed_ms="<<ure::monotonic_ms()-started<<'\n';
}
struct Workspace {
    ure::fs::path path;
    explicit Workspace(const ure::fs::path& parent) {
        auto text=(parent/"tree-listing-test-XXXXXX").string(); std::vector<char> bytes(text.begin(),text.end()); bytes.push_back('\0');
        const auto* directory=::mkdtemp(bytes.data()); check(directory,"Cannot create disk-backed fixture directory"); path=directory;
    }
    ~Workspace() { std::error_code error; ure::fs::remove_all(path,error); }
};
void child_wait(pid_t pid,int signal=0) {
    int state=0; while(::waitpid(pid,&state,0)<0)check(errno==EINTR,"Cannot wait for owned fixture child");
    check(signal ? WIFSIGNALED(state) && WTERMSIG(state)==signal : WIFEXITED(state) && WEXITSTATUS(state)==0,"Owned fixture child failed");
}
}
int main(int argc,char** argv) {
    try {
        if(argc==4 && std::string_view(argv[1])=="scan") { ure::Root source(argv[2]),store(argv[3]); scan(source.fd(),store.fd(),100000); return 0; }
        check(argc==2,"A disk-backed build directory is required"); Workspace work(argv[1]);
        ure::fs::create_directory(work.path/"source"); ure::Root source(work.path/"source"); auto store=ure::private_directory(work.path/"store",true);
        const std::vector<std::string> unusual{"z",std::string("\xff",1),"a\nline","a",std::string(255,'p')};
        for(const auto& name:unusual)create(source.fd(),name);
        auto expected=unusual; std::sort(expected.begin(),expected.end()); const auto initial_fds=descriptors();
        for(const int mode:{0,1,2}) {
            ure::TreeListingBudget budget; fault::mode=mode; fault::fallback=mode==2;
            {
                ure::SortedTreeDirectory listing(source.fd(),store.fd(),budget); std::string name;
                for(const auto& original:expected)check(listing.next(name) && name==original,"Opaque names or bytewise order changed");
                check(!listing.next(name),"Extra opaque name");
            }
            check(budget.memory==0 && budget.scratch==0 && descriptors()==initial_fds,"Successful listing leaked memory, scratch or descriptors");
        }
        fault::fallback=false;
        for(const int mode:{3,4}) {
            ure::TreeListingBudget budget; fault::mode=mode;
            reject([&] { ure::SortedTreeDirectory listing(source.fd(),store.fd(),budget); },"tree-listing-space");
            check(budget.memory==0 && budget.scratch==0 && descriptors()==initial_fds,"Failed scratch write/reserve leaked resources");
        }
        {
            ure::TreeListingBudget budget; ure::TreeListingMemory occupied(budget,ure::TreeListingBudget::memory_limit);
            reject([&] { ure::SortedTreeDirectory listing(source.fd(),store.fd(),budget,true); },"tree-listing-memory-limit");
            check(budget.admitted_entries==1 && budget.scratch==0 && descriptors()==initial_fds,"Memory refusal materialized children or leaked descriptors");
        }
        {
            ure::TreeListingBudget budget; budget.scratch=ure::TreeListingBudget::scratch_limit;
            reject([&] { ure::SortedTreeDirectory listing(source.fd(),store.fd(),budget); },"tree-listing-scratch-limit");
            check(budget.memory==0 && budget.scratch==ure::TreeListingBudget::scratch_limit && descriptors()==initial_fds,"Aggregate scratch refusal leaked resources");
        }
        ure::fs::create_directory(work.path/"wide"); ure::Root wide(work.path/"wide"); std::string name; name.reserve(255);
        for(unsigned index=0;index<100000;++index) { numbered_name(index,name); create(wide.fd(),name); }
        const auto child=::fork(); check(child>=0,"Cannot fork bounded scan fixture");
        if(child==0) { const auto exe=ure::fs::canonical("/proc/self/exe").string(); const auto a=(work.path/"wide").string(),b=(work.path/"store").string(); ::execl(exe.c_str(),exe.c_str(),"scan",a.c_str(),b.c_str(),static_cast<char*>(nullptr)); ::_exit(3); }
        child_wait(child);
        {
            ure::TreeListingBudget budget; budget.admitted_entries=ure::TreeListingBudget::entry_limit-1300;
            reject([&] { ure::SortedTreeDirectory listing(wide.fd(),store.fd(),budget,true); },"size-limit");
            check(budget.memory==0 && budget.scratch==0 && descriptors()==initial_fds+1,"Entry refusal after a populated run leaked resources");
        }
        int ready[2]; check(::pipe2(ready,O_CLOEXEC)==0,"Cannot create owned interruption pipe");
        const auto interrupted=::fork(); check(interrupted>=0,"Cannot fork enumeration interruption fixture");
        if(interrupted==0) { ::close(ready[0]); fault::mode=5; fault::barrier=ready[1]; try { ure::TreeListingBudget budget; ure::SortedTreeDirectory listing(wide.fd(),store.fd(),budget); ::_exit(2); } catch(...) { ::_exit(3); } }
        ::close(ready[1]); char recorded=0; struct pollfd notification{ready[0],POLLIN,0};
        const bool observed=::poll(&notification,1,5000)>0 && ::read(ready[0],&recorded,1)==1 && recorded=='R'; ::close(ready[0]);
        if(!observed) { ::kill(interrupted,SIGKILL); int ignored=0; while(::waitpid(interrupted,&ignored,0)<0 && errno==EINTR){} }
        check(observed,"No populated scratch write was observed");
        check(::kill(interrupted,SIGKILL)==0,"Cannot interrupt owned enumeration child"); child_wait(interrupted,SIGKILL);
        check(store.list(".").empty(),"Enumeration left a visible scratch record after interruption");
        create(wide.fd(),"one-too-many");
        { ure::TreeListingBudget budget; reject([&] { ure::SortedTreeDirectory listing(wide.fd(),store.fd(),budget); },"size-limit"); check(budget.memory==0 && budget.scratch==0,"Oversized directory leaked resources"); }
        const auto final_fds=descriptors(); std::vector<std::string> observed_names;
        {
            ure::Root independent(work.path/"source"); struct stat retained{},fresh{};
            check(::fstat(source.fd(),&retained)==0 && ::fstat(independent.fd(),&fresh)==0 && retained.st_dev==fresh.st_dev && retained.st_ino==fresh.st_ino,
                "Enumeration replaced or closed the retained source descriptor");
            observed_names=independent.list(".");
        }
        std::cout<<"Cleanup oracle: initial_fds="<<initial_fds<<", final_fds="<<final_fds<<", source_children="<<observed_names.size()
            <<", retained_source_offset="<<::lseek(source.fd(),0,SEEK_CUR)<<'\n';
        check(observed_names==expected,"Enumeration mutated the source namespace");
        check(final_fds==initial_fds+1,"Enumeration leaked descriptors");
        std::cout<<"Global preallocation budgets, opaque names, anonymous/fallback scratch, interrupted/short writes, space refusal, 100000-child boundary and observed SIGKILL passed; private synthetic directories only.\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
