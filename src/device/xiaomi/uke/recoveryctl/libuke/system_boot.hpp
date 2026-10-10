// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "lifecycle_policy.hpp"
#include <android-base/unique_fd.h>
#include <linux/fs.h>
#include <sys/ioctl.h>

namespace ure {
// Explicit system reboot may consume its recovery selector, never arbitrary
// misc data. The generic partition-metadata writer remains unavailable.
inline const char* prepare_system_boot(const LegacyLifecycleGuard& lifecycle) noexcept {
    if(!lifecycle.active() || dualboot_quarantine_state()!=DualbootQuarantineState::Clear)
        return "system-boot-operation-pending";
    const char* path="/dev/block/by-name/misc";
#ifdef URE_HOST_POLICY_FIXTURE
    path=::getenv("URE_SYSTEM_BOOT_MISC");
    if(!path || !*path)return "system-boot-misc-unavailable";
#endif
    android::base::unique_fd reader(::open(path,O_RDONLY|O_CLOEXEC|O_NONBLOCK));
    struct stat before{};
    if(reader.get()<0 || ::fstat(reader.get(),&before)!=0)return "system-boot-misc-unavailable";
    auto size=[](int fd,const struct stat& st,unsigned long long& bytes) {
#ifdef URE_HOST_POLICY_FIXTURE
        if(S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==::geteuid() && (st.st_mode&07777)==0600) {
            bytes=static_cast<unsigned long long>(st.st_size); return bytes>=65536;
        }
#endif
        return S_ISBLK(st.st_mode) && ::ioctl(fd,BLKGETSIZE64,&bytes)==0 && bytes>=65536;
    };
    unsigned long long before_size=0;
    if(!size(reader.get(),before,before_size))return "system-boot-misc-invalid";
    std::array<char,32> command{};
    auto read_command=[](int fd,std::array<char,32>& bytes) {
        ssize_t count;
        do { count=::pread(fd,bytes.data(),bytes.size(),0); } while(count<0 && errno==EINTR);
        return count==static_cast<ssize_t>(bytes.size());
    };
    if(!read_command(reader.get(),command))return "system-boot-command-unreadable";
    if(command[0]=='\0')return lifecycle.active() ? nullptr : "system-boot-operation-pending";
    if(::memcmp(command.data(),"boot-recovery",sizeof("boot-recovery"))!=0)
        return "system-boot-command-unrecognized";
    android::base::unique_fd writer(::open(path,O_RDWR|O_CLOEXEC|O_NONBLOCK));
    struct stat current{}; unsigned long long current_size=0;
    if(writer.get()<0 || ::fstat(writer.get(),&current)!=0 ||
       current.st_dev!=before.st_dev || current.st_ino!=before.st_ino || current.st_rdev!=before.st_rdev ||
       !size(writer.get(),current,current_size) || current_size!=before_size)
        return "system-boot-misc-changed";
    std::array<char,32> observed{},empty{};
    if(!read_command(writer.get(),observed) || observed!=command)
        return "system-boot-command-changed";
    if(!lifecycle.active() || dualboot_quarantine_state()!=DualbootQuarantineState::Clear)
        return "system-boot-operation-pending";
    ssize_t written;
    do { written=::pwrite(writer.get(),empty.data(),empty.size(),0); } while(written<0 && errno==EINTR);
    if(written!=static_cast<ssize_t>(empty.size()) || ::fsync(writer.get())!=0 ||
       !read_command(writer.get(),observed) || observed!=empty)
        return "system-boot-command-clear-failed";
    return nullptr;
}
} // namespace ure
