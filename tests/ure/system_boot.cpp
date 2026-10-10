// SPDX-License-Identifier: Apache-2.0
#include "system_boot.hpp"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace ure {
void* lifecycle_acquire(const char*,bool) noexcept {
    auto* lease=new RuntimeActivityLease(RuntimeActivityLease::acquire(nullptr,true));
    if(lease->valid())return lease;
    delete lease; return nullptr;
}
bool lifecycle_validate(void* token) noexcept { return static_cast<RuntimeActivityLease*>(token)->valid(); }
void lifecycle_release(void* token) noexcept { delete static_cast<RuntimeActivityLease*>(token); }
bool lifecycle_stage_reboot() noexcept { return false; }
}
extern "C" int __real_open(const char*,int,...);
extern "C" ssize_t __real_pread(int,void*,size_t,off_t);
extern "C" ssize_t __real_pwrite(int,const void*,size_t,off_t);
extern "C" int __real_fsync(int);
namespace {
std::string path;
unsigned reads=0,opens=0,writes=0,passed=0;
enum class Fault { None,Open,Read,Changed,Write,ShortWrite,Sync,Readback };
Fault fault=Fault::None;
bool is_misc(int fd) {
    struct stat fd_st{},path_st{};
    return ::fstat(fd,&fd_st)==0 && ::stat(path.c_str(),&path_st)==0 &&
        fd_st.st_dev==path_st.st_dev && fd_st.st_ino==path_st.st_ino;
}
void require(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
std::vector<char> load() {
    android::base::unique_fd fd(__real_open(path.c_str(),O_RDONLY));
    struct stat st{}; require(fd.get()>=0 && ::fstat(fd.get(),&st)==0,"fixture read open");
    std::vector<char> bytes(static_cast<size_t>(st.st_size));
    require(__real_pread(fd.get(),bytes.data(),bytes.size(),0)==static_cast<ssize_t>(bytes.size()),"fixture read");
    return bytes;
}
void seed(const char* command="boot-recovery",size_t size=4194304) {
    android::base::unique_fd fd(__real_open(path.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600));
    std::vector<char> bytes(size);
    for(size_t i=0;i<size;++i)bytes[i]=static_cast<char>((i*17+29)%251);
    std::memset(bytes.data(),0,32); std::memcpy(bytes.data(),command,std::strlen(command));
    require(fd.get()>=0 && __real_pwrite(fd.get(),bytes.data(),bytes.size(),0)==static_cast<ssize_t>(size),"fixture seed");
    require(__real_fsync(fd.get())==0,"fixture sync");
    reads=opens=writes=0; fault=Fault::None;
}
void check(const char* name,const char* expected,Fault injection=Fault::None,bool changed=false) {
    const auto before=load(); fault=injection; reads=opens=writes=0;
    ure::LegacyLifecycleGuard lease("reboot");
    const auto* error=ure::prepare_system_boot(lease);
    require(expected ? error && std::strcmp(error,expected)==0 : error==nullptr,name);
    const auto after=load();
    require(before.size()==after.size() && std::equal(before.begin()+32,before.end(),after.begin()+32),"non-command misc bytes changed");
    if(changed)require(std::all_of(after.begin(),after.begin()+32,[](char c){return c==0;}),"command was not cleared");
    else require(before==after,"refused or empty command mutated misc");
    require(writes==(changed || injection==Fault::Write || injection==Fault::ShortWrite ? 1u : 0u),"unexpected write count");
    if(expected && (std::strcmp(expected,"system-boot-command-unrecognized")==0 ||
        std::strcmp(expected,"system-boot-operation-pending")==0 || std::strcmp(expected,"system-boot-misc-invalid")==0))
        require(opens==0,"refused operation opened misc writable");
    ++passed; std::printf("PASS %s\n",name);
}
}
extern "C" int __wrap_open(const char* name,int flags,...) {
    mode_t mode=0;
    if(flags&O_CREAT) { va_list arguments; va_start(arguments,flags); mode=va_arg(arguments,int); va_end(arguments); }
    if(name && path==name && (flags&O_ACCMODE)!=O_RDONLY) {
        ++opens; require((flags&(O_CREAT|O_TRUNC|O_APPEND))==0,"unsafe misc open flags");
        if(fault==Fault::Open) { errno=EACCES; return -1; }
    }
    return __real_open(name,flags,mode);
}
extern "C" ssize_t __wrap_pread(int fd,void* data,size_t count,off_t offset) {
    const bool misc=is_misc(fd);
    if(misc) {
        ++reads;
        if(fault==Fault::Read) { errno=EIO; return -1; }
    }
    const auto result=__real_pread(fd,data,count,offset);
    if(misc && result>0 && ((fault==Fault::Changed && reads==2) || (fault==Fault::Readback && reads==3)))
        static_cast<char*>(data)[0]='X';
    return result;
}
extern "C" ssize_t __wrap_pwrite(int fd,const void* data,size_t count,off_t offset) {
    require(is_misc(fd) && count==32 && offset==0,"writer escaped the command field"); ++writes;
    if(fault==Fault::Write) { errno=EIO; return -1; }
    if(fault==Fault::ShortWrite)return 0;
    return __real_pwrite(fd,data,count,offset);
}
extern "C" int __wrap_fsync(int fd) {
    if(is_misc(fd) && fault==Fault::Sync) { errno=EIO; return -1; }
    return __real_fsync(fd);
}
int main(int argc,char** argv) {
    try {
        require(argc==2,"pass a private fixture directory"); const std::string root=argv[1]; path=root+"/misc";
        require(::setenv("URE_SYSTEM_BOOT_MISC",path.c_str(),1)==0 &&
            ::setenv("URE_GUI_JOB_REGISTRY",(root+"/registry").c_str(),1)==0 &&
            ::setenv("URE_DUALBOOT_QUARANTINE",(root+"/quarantine").c_str(),1)==0,"fixture environment");
        seed(); check("known recovery command",nullptr,Fault::None,true);
        check("repeated system boot is a no-op",nullptr);
        seed(); {
            const int fd=__real_open(path.c_str(),O_RDWR); require(fd>=0,"stale tail open");
            require(__real_pwrite(fd,"oader",5,14)==5,"stale command tail"); ::close(fd);
        }
        check("NUL-terminated selector with stale tail",nullptr,Fault::None,true);
        seed(""); check("empty selector preserves the whole misc partition",nullptr); require(opens==0,"empty selector opened writable");
        for(const char* command:{"bootonce-bootloader","boot-fastboot","boot-recoveryX","wipe_data"}) {
            seed(command); check(command,"system-boot-command-unrecognized");
        }
        seed("boot-recovery",4096); check("short misc refused","system-boot-misc-invalid");
        seed(); require(::chmod(path.c_str(),0644)==0,"chmod fixture");
        check("unsafe fixture mode refused","system-boot-misc-invalid"); require(::chmod(path.c_str(),0600)==0,"restore mode");
        require(::link(path.c_str(),(root+"/hardlink").c_str())==0,"hardlink fixture");
        check("hardlinked fixture refused","system-boot-misc-invalid"); require(::unlink((root+"/hardlink").c_str())==0,"remove hardlink");
        for(const auto injection:{Fault::Open,Fault::Read,Fault::Changed,Fault::Write,Fault::ShortWrite,Fault::Sync,Fault::Readback}) {
            seed(); const char* expected=injection==Fault::Open ? "system-boot-misc-changed" :
                injection==Fault::Read ? "system-boot-command-unreadable" :
                injection==Fault::Changed ? "system-boot-command-changed" : "system-boot-command-clear-failed";
            check(expected,expected,injection,injection==Fault::Sync || injection==Fault::Readback);
        }
        seed(); {
            auto active=ure::RuntimeActivityLease::acquire("11111111111111111111111111111111");
            require(active.valid(),"active job fixture"); check("active job blocks system boot","system-boot-operation-pending");
        }
        require(::mkdir((root+"/quarantine").c_str(),0700)==0,"quarantine fixture");
        for(const auto state:{ure::DualbootQuarantineState::Pending,ure::DualbootQuarantineState::Committed,ure::DualbootQuarantineState::GptRestored}) {
            const auto* marker=ure::dualboot_quarantine_marker(state);
            const int fd=__real_open((root+"/quarantine/phase").c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);
            require(fd>=0 && ::write(fd,marker,std::strlen(marker))==static_cast<ssize_t>(std::strlen(marker)),"quarantine marker"); ::close(fd);
            check("quarantine preserves persistent recovery selector","system-boot-operation-pending");
        }
        require(::unlink((root+"/quarantine/phase").c_str())==0,"invalid quarantine setup");
        check("invalid quarantine blocks system boot","system-boot-operation-pending");
        std::printf("RESULT %u cases; only the 32-byte command field may change.\n",passed);
        return 0;
    } catch(const std::exception& error) { std::fprintf(stderr,"FAIL %s\n",error.what()); return 1; }
}
