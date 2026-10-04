// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <array>
#include <cerrno>
#include <charconv>
#include <fcntl.h>
#include <iostream>
#include <linux/fs.h>
#include <sys/statvfs.h>
#include <unistd.h>

// These linker boundaries exist only in this host executable. Production tools
// still use their fixed executable paths and production ownership coordinator.
static bool no_journal_space=false, staging_full=false, partial_tool=false;
static dev_t original_device{};
static ino_t original_inode{};
static std::uint64_t staged_written=0;
static bool working_file(int fd);
extern "C" int __real_fstatvfs(int,struct statvfs*);
extern "C" ssize_t __real_pwrite(int,const void*,size_t,off_t);
extern "C" int __real_execve(const char*,char* const[],char* const[]);
extern "C" int __real_ioctl(int,unsigned long,...);
// Linux ioctl passes its third argument in one machine-word slot. All fixture
// targets are regular files; no block-device ioctl is invoked by this test.
extern "C" int __wrap_ioctl(int fd,unsigned long request,void* argument) {
    if(staging_full && request==FICLONE && working_file(fd)) { errno=EOPNOTSUPP; return -1; }
    return __real_ioctl(fd,request,argument);
}
extern "C" int __wrap_fstatvfs(int fd,struct statvfs* value) {
    const auto result=__real_fstatvfs(fd,value);
    if(result==0 && no_journal_space)value->f_bavail=0;
    return result;
}
static bool working_file(int fd) {
    std::array<char,4096> path{};
    const auto link="/proc/self/fd/"+std::to_string(fd);
    const auto length=::readlink(link.c_str(),path.data(),path.size());
    struct stat st{};
    return length>0 && static_cast<std::size_t>(length)<path.size() &&
        std::string_view(path.data(),static_cast<std::size_t>(length)).ends_with("/working.img") &&
        ::fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_nlink==1 &&
        (st.st_mode&07777)==0600 && st.st_uid==::geteuid() &&
        (st.st_dev!=original_device || st.st_ino!=original_inode);
}
extern "C" ssize_t __wrap_pwrite(int fd,const void* data,size_t size,off_t offset) {
    if(staging_full && offset>=1024*1024 && working_file(fd)) { errno=ENOSPC; return -1; }
    const auto result=__real_pwrite(fd,data,size,offset);
    if(result>0 && staging_full && working_file(fd))staged_written+=static_cast<std::uint64_t>(result);
    return result;
}
extern "C" int __wrap_execve(const char* file,char* const argv[],char* const environment[]) {
    if(partial_tool && std::string_view(file).ends_with("/e2fsck")) {
        int fd=-1;
        for(std::size_t i=1;argv[i];++i) {
            const std::string_view arg(argv[i]);
            constexpr std::string_view prefix="/proc/self/fd/";
            if(!arg.starts_with(prefix))continue;
            const auto tail=arg.substr(prefix.size());
            const auto parsed=std::from_chars(tail.data(),tail.data()+tail.size(),fd);
            if(parsed.ec!=std::errc{} || parsed.ptr!=tail.data()+tail.size())::_exit(91);
        }
        if(fd<3 || !working_file(fd))::_exit(92);
        const std::array<char,4096> damage{'P','A','R','T','I','A','L'};
        if(__real_pwrite(fd,damage.data(),damage.size(),4096)!=static_cast<ssize_t>(damage.size()) || ::fsync(fd)!=0)::_exit(93);
        constexpr char output[]="Host fixture: private staging changed, then tool failed\n";
        if(::write(STDOUT_FILENO,output,sizeof(output)-1)<0)::_exit(94);
        ::_exit(42);
    }
    return __real_execve(file,argv,environment);
}

namespace {
void check(bool condition,const std::string& message) { if(!condition)throw std::runtime_error(message); }
template<class F> void reject(F function,const std::string& code) {
    try { function(); } catch(const ure::Error& error) {
        check(error.code==code,"Unexpected rejection: "+error.code+", wanted "+code); return;
    }
    throw std::runtime_error("Expected rejection: "+code);
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto pattern=(ure::fs::path(argv[1])/"filesystem-failures-XXXXXX").string();
    const auto directory=::mkdtemp(pattern.data()); if(!directory)return 1;
    const ure::fs::path work(directory);
    try {
        ::umask(0077);
        ure::fs::create_directory(work/"system"); ure::Root system(work/"system");
        ure::Fd original(::open((work/"source.img").c_str(),O_RDWR|O_CREAT|O_EXCL,0600));
        check(original.get()>=0 && ::ftruncate(original.get(),64*1024*1024)==0,"Cannot create regular fixture");
        const auto made=ure::run_tool("mke2fs",{"-q","-F","-t","ext4","-O","^encrypt,^orphan_file",(work/"source.img").string()},60);
        check(made.status==0 && !made.timed_out,"Cannot format regular fixture");
        struct stat st{}; check(::fstat(original.get(),&st)==0,"Cannot identify original fixture");
        original_device=st.st_dev; original_inode=st.st_ino;
        const auto before=ure::sha256(original.get());
        for(const auto& kind:{"admission-space","staging-space","partial-tool"}) {
            auto target=ure::storage_image(work/"source.img",512,true);
            ure::Value request; request["schema"]=1; request["action"]="repair"; request["filesystem"]="ext4";
            const auto plan=ure::filesystem_operation_plan(system,target,request,"fixture-profile");
            const auto journal=work/kind;
            no_journal_space=std::string_view(kind)=="admission-space";
            staging_full=std::string_view(kind)=="staging-space";
            partial_tool=std::string_view(kind)=="partial-tool";
            const auto expected=no_journal_space ? "insufficient-space" : staging_full ? "io-error" : "filesystem-tool-failed";
            reject([&]{ure::filesystem_operation_execute(system,target,plan,journal,plan["plan_sha256"].asString());},expected);
            no_journal_space=false; staging_full=false; partial_tool=false;
            check(ure::sha256(original.get())==before,"Failed preparation changed original bytes");
            const auto inspected=ure::filesystem_operation_recover(system,target,journal,"inspect","");
            check(inspected["original_unchanged_verified"]==true && inspected["state"]=="FAILED_SAFE" &&
                inspected["error_code"]==expected,"Failed preparation did not retain inspectable state");
            ure::Root store(journal);
            check(!store.exists("application"),"Failed preparation reached target application");
            if(std::string_view(kind)=="staging-space") {
                auto staged=store.open("working.img",O_RDONLY);
                check(staged_written==1024*1024 && ure::storage_read(staged.get(),0,1024*1024)==ure::storage_read(original.get(),0,1024*1024),
                    "ENOSPC was not injected after a real staged copy");
            }
            if(std::string_view(kind)=="partial-tool") {
                check(inspected["tool_result"]["exit_status"]==42,"Wrong partial-tool failure result");
                auto staged=store.open("working.img",O_RDONLY);
                check(ure::storage_read(staged.get(),4096,7)=="PARTIAL","Tool did not really alter private staging");
            }
            reject([&]{ure::filesystem_operation_recover(system,target,journal,"resume",plan["plan_sha256"].asString());},"prepare-restart-required");
            const auto cancelled=ure::filesystem_operation_recover(system,target,journal,"cancel",plan["plan_sha256"].asString());
            check(cancelled["state"]=="CANCELLED_SAFE" && cancelled["original_unchanged_verified"]==true,
                "Failed preparation cannot be safely cancelled");
            check(ure::sha256(original.get())==before && target.identity["file_inode"].asUInt64()==static_cast<std::uint64_t>(st.st_ino),
                "Cancellation changed the original image");
            std::cout<<kind<<": actual staged boundary, retained owner, inspection and verified cancellation passed\n";
        }
        ure::fs::remove_all(work); return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<"; private failed fixture retained at "<<work<<'\n'; return 1;
    }
}
