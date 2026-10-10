// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the actual loader with fabricated descriptors; never create a loop device.
#include <cassert>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/fs.h>
#include <linux/loop.h>
#include <map>
#include <regex>
#include <string>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <vector>

enum class Failure { None, Control, Free, Node, Payload, Stat, Empty, Special,
                     Loop, Attach, Status, Flush, BlockSize, Directory, Mount };
static Failure failure;
static int next_fd, next_loop, current_loop, clears, mounts;
static bool existing_node;
struct Descriptor { std::string path; bool bound=false, autoclear=false, mounted=false; };
static std::map<int, Descriptor> descriptors;
static std::vector<int> mounted_loops;
static int reject() { errno=EIO; return -1; }
static int fake_open(const char* path, int flags) {
    const std::string name(path);
    assert(flags&O_CLOEXEC);
    if ((name=="/dev/loop-control" && failure==Failure::Control) ||
        (name.rfind("/tmp/",0)==0 && failure==Failure::Payload) ||
        (name.rfind("/dev/block/",0)==0 && failure==Failure::Loop)) return reject();
    if (name.rfind("/dev/block/",0)==0)
        assert(name=="/dev/block/loop"+std::to_string(current_loop));
    const int fd=next_fd++;
    descriptors.emplace(fd,Descriptor{name});
    return fd;
}
static int fake_close(int fd) {
    auto found=descriptors.find(fd);
    assert(found!=descriptors.end());
    assert(!found->second.bound || found->second.autoclear || found->second.mounted);
    descriptors.erase(found);
    return 0;
}
static int fake_fstat(int fd, struct stat* result) {
    assert(descriptors.count(fd));
    if (failure==Failure::Stat) return reject();
    *result={};
    result->st_mode=failure==Failure::Special ? S_IFBLK : S_IFREG;
    result->st_size=failure==Failure::Empty ? 0 : 8192;
    return 0;
}
static off_t fake_lseek(int fd, off_t, int) {
    if (!descriptors.count(fd)) { errno=EBADF; return -1; }
    return 8192;
}
static int fake_ioctl(int fd, unsigned long request, ...) {
    auto& owner=descriptors.at(fd);
    if (request==LOOP_CTL_GET_FREE) {
        if (failure==Failure::Free) return reject();
        current_loop=next_loop; next_loop+=4;
        return current_loop;
    }
    va_list args; va_start(args,request);
    int result=0;
    if (request==LOOP_SET_FD) {
        const int payload=va_arg(args,int);
        assert(descriptors.count(payload));
        if (failure==Failure::Attach) result=reject();
        else owner.bound=true;
    } else if (request==LOOP_SET_STATUS64) {
        const auto* info=va_arg(args,const struct loop_info64*);
        assert(owner.bound);
        assert(info->lo_sizelimit==8192);
        assert(info->lo_flags==(LO_FLAGS_READ_ONLY|LO_FLAGS_AUTOCLEAR));
        if (failure==Failure::Status) result=reject();
        else owner.autoclear=true;
    } else if (request==LOOP_CLR_FD) {
        assert(owner.bound); owner.bound=false; ++clears;
    } else if (request==BLKFLSBUF) {
        if (failure==Failure::Flush) result=reject();
    } else if (request==LOOP_SET_BLOCK_SIZE) {
        assert(va_arg(args,int)==4096);
        if (failure==Failure::BlockSize) result=reject();
    } else assert(false);
    va_end(args);
    return result;
}
static int fake_mknod(const char* path, mode_t mode, dev_t device) {
    assert(std::string(path)=="/dev/block/loop"+std::to_string(current_loop));
    assert(S_ISBLK(mode) && major(device)==7 && minor(device)==static_cast<unsigned>(current_loop));
    return failure==Failure::Node ? reject() : 0;
}
static int fake_mkdir(const char*, mode_t mode) {
    assert(mode==0755);
    return failure==Failure::Directory ? reject() : 0;
}
static int fake_rmdir(const char*) { return 0; }
static int fake_mount(const char* device, const char* target, const char* fs,
                      unsigned long flags, const void*) {
    assert(std::string(device)=="/dev/block/loop"+std::to_string(current_loop));
    assert(std::string(target).rfind("/apex/com.android.",0)==0);
    assert(std::string(fs)=="ext4" && flags==MS_RDONLY);
    if (failure==Failure::Mount) return reject();
    bool found=false;
    for (auto& [fd,owner]:descriptors) {
        (void)fd;
        if (owner.path==device) { assert(owner.bound && owner.autoclear); owner.mounted=true; found=true; }
    }
    assert(found); ++mounts; mounted_loops.push_back(current_loop);
    return 0;
}
static char* fake_basename(const char* path) { return const_cast<char*>(strrchr(path,'/')+1); }
static size_t fake_strlcpy(char* output, const char* text, size_t capacity) {
    const auto size=strlen(text);
    assert(size<capacity); memcpy(output,text,size+1); return size;
}
namespace TWFunc { static bool Path_Exists(const std::string&) { return existing_node; } }
class twrpApex {
public:
    bool mountApexOnLoopbackDevices(std::vector<std::string>);
    bool loadApexImage(std::string,size_t);
    std::string unzipImage(const std::string& file) {
        return file=="missing" ? "" : "/tmp/"+file;
    }
};
#define LOOP_CONTROL "/dev/loop-control"
#define LOOP_BLOCK_DEVICE_DIR "/dev/block/"
#define APEX_BASE "/apex/"
#define LOGERR(...) ((void)0)
#define LOGINFO(...) ((void)0)
#define open fake_open
#define close fake_close
#define fstat fake_fstat
#define lseek fake_lseek
#define ioctl fake_ioctl
#define mknod fake_mknod
#define mkdir fake_mkdir
#define rmdir fake_rmdir
#define mount fake_mount
#define basename fake_basename
#define strlcpy fake_strlcpy
#include "apex-loop.inc"

int main() {
    for (const auto fail : {Failure::None,Failure::Control,Failure::Free,Failure::Node,
             Failure::Payload,Failure::Stat,Failure::Empty,Failure::Special,Failure::Loop,
             Failure::Attach,Failure::Status,Failure::Flush,Failure::BlockSize,
             Failure::Directory,Failure::Mount}) {
        failure=fail; next_fd=100; next_loop=7; current_loop=-1; clears=mounts=0;
        descriptors.clear(); mounted_loops.clear(); existing_node=false;
        twrpApex loader;
        const bool result=loader.mountApexOnLoopbackDevices({"missing","com.android.one.apex","com.android.two.apex"});
        const bool expected=fail==Failure::None || fail==Failure::BlockSize;
        assert(result==expected);
        assert(descriptors.empty());
        assert(mounts==(expected ? 2 : 0));
        if (expected) assert((mounted_loops==std::vector<int>{7,11}));
        if (fail==Failure::Status) assert(clears==1);
    }
    failure=Failure::None; next_fd=100; next_loop=7; mounts=0; existing_node=true;
    twrpApex loader;
    assert(loader.mountApexOnLoopbackDevices({"com.android.existing.apex"}));
    assert(descriptors.empty() && mounts==1);
    assert(loader.mountApexOnLoopbackDevices({"missing"}));
    assert(descriptors.empty() && mounts==1);
    std::puts("APEX loop allocation, payload bounds and failure cleanup controls passed.");
}
