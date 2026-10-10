// SPDX-License-Identifier: Apache-2.0
// Real private-directory renames with exact-descriptor Btrfs identity stand-ins.
// No Btrfs kernel, host block node or tablet operation is exercised here.
#include "uke.h"
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <fcntl.h>
#include <iostream>
#include <linux/btrfs.h>
#include <linux/magic.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
struct Volume { std::uint64_t tree; std::uint64_t generation=1; };
std::map<ino_t,Volume> volumes;
ure::fs::path selected_root;
ino_t parent_inode=0;
bool fail_parent_sync=false,inject_collision=false;
unsigned rename_calls=0;
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
template<class F> void reject(F function,const std::string& expected) {
    try { function(); } catch(const ure::Error& error) {
        check(error.code==expected,("Unexpected refusal: "+error.code+", expected "+expected).c_str()); return;
    }
    throw std::runtime_error("Expected refusal: "+expected);
}
void write(const ure::fs::path& path,const char* contents) { std::ofstream file(path); file<<contents; check(file.good(),"Cannot write private payload"); }
std::string fd_path(int fd) {
    char bytes[4096]{}; const auto n=::readlink(("/proc/self/fd/"+std::to_string(fd)).c_str(),bytes,sizeof(bytes));
    return n>0 ? std::string(bytes,static_cast<std::size_t>(n)) : std::string{};
}
} // namespace
extern "C" int __real_fstat(int,struct stat*);
extern "C" int __real_fstatfs(int,struct statfs*);
extern "C" int __real_fsync(int);
extern "C" int __wrap_fstat(int fd,struct stat* info) {
    const auto result=__real_fstat(fd,info);
    if(result==0 && volumes.contains(info->st_ino)) { const auto id=volumes.at(info->st_ino).tree;
        info->st_ino=256; info->st_dev=static_cast<dev_t>(0xff0000+id); }
    return result;
}
extern "C" int __wrap_fstatfs(int fd,struct statfs* info) {
    const auto result=__real_fstatfs(fd,info); const auto path=fd_path(fd);
    if(result==0 && (path==selected_root.string() || path.starts_with(selected_root.string()+"/")))info->f_type=BTRFS_SUPER_MAGIC;
    return result;
}
extern "C" int __wrap_ioctl(int fd,unsigned long request,...) {
    struct stat observed{};
    if(__real_fstat(fd,&observed)<0) { errno=EBADF; return -1; }
    const auto path=fd_path(fd);
    if(path!=selected_root.string() && !path.starts_with(selected_root.string()+"/")) { errno=ENOTTY; return -1; }
    va_list arguments; va_start(arguments,request); auto* payload=va_arg(arguments,void*); va_end(arguments);
    if(request==BTRFS_IOC_FS_INFO) {
        auto* info=static_cast<btrfs_ioctl_fs_info_args*>(payload); std::memset(info,0,sizeof(*info));
        info->num_devices=1; info->max_id=1; std::memset(info->fsid,0x42,sizeof(info->fsid)); return 0;
    }
    if(!volumes.contains(observed.st_ino)) { errno=ENOTTY; return -1; }
    if(request==BTRFS_IOC_SUBVOL_GETFLAGS) { *static_cast<__u64*>(payload)=0; return 0; }
    if(request==BTRFS_IOC_GET_SUBVOL_INFO) {
        auto* info=static_cast<btrfs_ioctl_get_subvol_info_args*>(payload); std::memset(info,0,sizeof(*info));
        const auto volume=volumes.at(observed.st_ino); info->treeid=volume.tree; info->generation=volume.generation; info->ctransid=volume.generation;
        std::memset(info->uuid,static_cast<int>(volume.tree),sizeof(info->uuid));
        const auto name=ure::fs::path(path).filename().string(); std::memcpy(info->name,name.c_str(),name.size()+1); return 0;
    }
    errno=ENOTTY; return -1;
}
extern "C" int __wrap_fsync(int fd) {
    struct stat observed{};
    if(inject_collision && __real_fstat(fd,&observed)==0 && S_ISDIR(observed.st_mode) && fd_path(fd)!=selected_root.string() &&
        ure::fs::exists(ure::fs::path(fd_path(fd))/"state.json")) {
        ure::Root store(ure::Fd(::fcntl(fd,F_DUPFD_CLOEXEC,0)));
        if(ure::parse_json(store.read("state.json"))["state"]=="RENAMING") {
            inject_collision=false; ure::fs::create_directory(selected_root/"renamed"); write(selected_root/"renamed/marker","destination remains\n");
        }
    }
    if(fail_parent_sync && __real_fstat(fd,&observed)==0 && observed.st_ino==parent_inode && !ure::fs::exists(selected_root/"source")) {
        fail_parent_sync=false; errno=EIO; return -1;
    }
    return __real_fsync(fd);
}
int main(int argc,char** argv) {
    auto pattern=(ure::fs::path(argc>1 ? argv[1] : ".")/"ure-btrfs-rename-XXXXXX").string();
    std::vector<char> bytes(pattern.begin(),pattern.end()); bytes.push_back('\0'); const auto* made=::mkdtemp(bytes.data()); if(!made)return 1;
    const ure::fs::path work=ure::fs::absolute(made);
    try {
        for(const auto scenario:{0,1,2,3,4,5}) {
            volumes.clear(); selected_root=work/("root-"+std::to_string(scenario)); ure::fs::create_directory(selected_root);
            ure::fs::create_directory(selected_root/"source"); write(selected_root/"source/payload","original contents\n");
            struct stat observed{}; check(::stat(selected_root.c_str(),&observed)==0,"Cannot inspect root"); parent_inode=observed.st_ino; volumes[observed.st_ino]={5,1};
            check(::stat((selected_root/"source").c_str(),&observed)==0,"Cannot inspect source"); const auto source_inode=observed.st_ino; volumes[source_inode]={256,1};
            ure::Root root(selected_root); ure::Value request; request["schema"]=1; request["action"]="rename"; request["path"]="source"; request["new_path"]="renamed";
            auto invalid=request; invalid["new_path"]="../outside"; reject([&] { ure::btrfs_manage_plan(root,invalid,"host-ioctl-fixture"); },"invalid-path");
            invalid["new_path"]="source"; reject([&] { ure::btrfs_manage_plan(root,invalid,"host-ioctl-fixture"); },"existing-subvolume");
            invalid["new_path"]="source/child"; reject([&] { ure::btrfs_manage_plan(root,invalid,"host-ioctl-fixture"); },"invalid-rename-path");
            const auto plan=ure::btrfs_manage_plan(root,request,"host-ioctl-fixture"); const auto journal=work/("journal-"+std::to_string(scenario));
            reject([&] { ure::btrfs_manage_execute(root,plan,journal,std::string(64,'0')); },"confirmation-required");
            if(scenario==1) {
                ++volumes[source_inode].generation;
                reject([&] { ure::btrfs_manage_execute(root,plan,journal,plan["plan_sha256"].asString()); },"stale-btrfs-plan");
                check(root.exists("source") && !root.exists("renamed") && !ure::fs::exists(journal),"Stale source caused an effect"); continue;
            }
            if(scenario==2) {
                ure::fs::create_directory(selected_root/"renamed"); write(selected_root/"renamed/marker","destination remains\n");
                reject([&] { ure::btrfs_manage_execute(root,plan,journal,plan["plan_sha256"].asString()); },"existing-subvolume");
                check(root.read("renamed/marker")=="destination remains\n" && root.exists("source") && !ure::fs::exists(journal),"Existing destination was overwritten"); continue;
            }
            if(scenario==5) {
                check(::symlink("source",(selected_root/"renamed").c_str())==0,"Cannot create private symlink");
                reject([&] { ure::btrfs_manage_execute(root,plan,journal,plan["plan_sha256"].asString()); },"existing-subvolume");
                check(root.exists("source") && !ure::fs::exists(journal),"Symlink destination was followed"); continue;
            }
            fail_parent_sync=scenario==3; inject_collision=scenario==4;
            if(scenario==3 || scenario==4) {
                reject([&] { ure::btrfs_manage_execute(root,plan,journal,plan["plan_sha256"].asString()); },"subvolume-rename-failed");
                check(ure::parse_json(ure::Root(journal).read("state.json"))["state"]=="RENAMING","Failed rename lost its durable phase");
                if(scenario==3)check(!root.exists("source") && root.read("renamed/payload")=="original contents\n","Sync failure lost the renamed source");
                else { check(root.exists("source") && root.read("renamed/marker")=="destination remains\n","Racing destination was overwritten"); ure::fs::remove_all(selected_root/"renamed"); }
            }
            const auto completed=ure::btrfs_manage_execute(root,plan,journal,plan["plan_sha256"].asString());
            check(completed["state"]=="COMPLETE" && completed["operation_owner_released"]==true && !root.exists("source") &&
                root.read("renamed/payload")=="original contents\n" && completed["renamed_identity"]["uuid"]==plan["source_identity"]["uuid"],"Rename/resume lost payload, identity or ownership");
            check(ure::btrfs_manage_execute(root,plan,journal,plan["plan_sha256"].asString())["state"]=="COMPLETE","Completed rename was not verified on replay");
            ++rename_calls;
        }
        check(rename_calls==3,"Expected ordinary rename and both interrupted resume cases");
        ure::fs::remove_all(work); std::cout<<"PASS native Btrfs rename: root confinement, consent, stale source, no overwrite, symlink refusal, parent-sync failure, racing destination and exact interrupted resume; private directories with ioctl identity stand-ins only\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<"\nPrivate rename fixture retained: "<<work<<'\n'; return 1; }
}
