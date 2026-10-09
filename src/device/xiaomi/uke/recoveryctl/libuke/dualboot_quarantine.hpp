// SPDX-License-Identifier: Apache-2.0
#pragma once
// Volatile recovery quarantine for the narrowed dualboot writer. This header
// also serves fastbootd without JsonCpp, libcrypto, exceptions or RTTI. The
// writer must retain the cooperating runtime lifecycle lease through every
// write and checkpoint. A marker alone is not a kernel block-device claim,
// durable reboot recovery, or protection against a privileged raw shell.
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ure {
enum class DualbootQuarantineState { Clear, Pending, Committed, GptRestored, Invalid };
namespace dualboot_quarantine_detail {
constexpr std::size_t path_limit=4096,marker_limit=96;
constexpr const char* phase_name="phase";
inline const char* path() noexcept {
#ifdef URE_HOST_POLICY_FIXTURE
    // Host policy fixtures cannot change the shipping quarantine location.
    if(const auto* fixture=::getenv("URE_DUALBOOT_QUARANTINE"))return fixture;
#endif
    return "/tmp/uke-dualboot";
}
inline uid_t owner() noexcept {
#ifdef URE_HOST_POLICY_FIXTURE
    return ::geteuid();
#else
    return 0;
#endif
}
inline bool private_directory(int fd,struct stat& observed) noexcept {
    return ::fstat(fd,&observed)==0 && S_ISDIR(observed.st_mode) &&
        observed.st_uid==owner() && (observed.st_mode&07777)==0700;
}
inline bool private_marker(int fd,struct stat& observed) noexcept {
    return ::fstat(fd,&observed)==0 && S_ISREG(observed.st_mode) && observed.st_nlink==1 &&
        observed.st_uid==owner() && (observed.st_mode&07777)==0600 &&
        observed.st_size>0 && static_cast<unsigned long long>(observed.st_size)<marker_limit;
}
inline bool same_file(const struct stat& first,const struct stat& second) noexcept {
    return first.st_dev==second.st_dev && first.st_ino==second.st_ino && first.st_mode==second.st_mode &&
        first.st_uid==second.st_uid && first.st_nlink==second.st_nlink && first.st_size==second.st_size &&
        first.st_mtim.tv_sec==second.st_mtim.tv_sec && first.st_mtim.tv_nsec==second.st_mtim.tv_nsec &&
        first.st_ctim.tv_sec==second.st_ctim.tv_sec && first.st_ctim.tv_nsec==second.st_ctim.tv_nsec;
}
// Missing means only that the final quarantine directory is absent. Missing
// parents, symlinks, inaccessible components and malformed fixture paths deny.
inline int directory(bool& missing) noexcept {
    missing=false; const auto* selected=path();
    if(!selected || selected[0]!='/' || ::strnlen(selected,path_limit)>=path_limit)return -1;
    const auto length=::strlen(selected); if(length<2 || selected[length-1]=='/')return -1;
    int current=::open("/",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(current<0)return -1;
    const char* first=selected+1;
    while(*first) {
        const char* end=::strchr(first,'/'); const auto count=end ? static_cast<std::size_t>(end-first) : ::strlen(first);
        std::array<char,256> part{};
        if(count==0 || count>=part.size() || (count==1 && first[0]=='.') || (count==2 && first[0]=='.' && first[1]=='.')) {
            ::close(current); return -1;
        }
        for(std::size_t i=0;i<count;++i)if(static_cast<unsigned char>(first[i])<32 || static_cast<unsigned char>(first[i])==127) {
            ::close(current); return -1;
        }
        ::memcpy(part.data(),first,count);
        const int next=::openat(current,part.data(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
        const int failure=errno; ::close(current);
        if(next<0) { missing=!end && failure==ENOENT; return -1; }
        current=next; if(!end)break; first=end+1;
    }
    struct stat observed{};
    if(!private_directory(current,observed)) { ::close(current); return -1; }
    return current;
}
inline DualbootQuarantineState decode(const char* bytes,std::size_t length) noexcept {
    constexpr const char pending[]="URE-DUALBOOT-QUARANTINE-V1\nPENDING\n";
    constexpr const char committed[]="URE-DUALBOOT-QUARANTINE-V1\nCOMMITTED\n";
    constexpr const char restored[]="URE-DUALBOOT-QUARANTINE-V1\nGPT_RESTORED\n";
    if(length==sizeof(pending)-1 && ::memcmp(bytes,pending,length)==0)return DualbootQuarantineState::Pending;
    if(length==sizeof(committed)-1 && ::memcmp(bytes,committed,length)==0)return DualbootQuarantineState::Committed;
    if(length==sizeof(restored)-1 && ::memcmp(bytes,restored,length)==0)return DualbootQuarantineState::GptRestored;
    return DualbootQuarantineState::Invalid;
}
}
inline const char* dualboot_quarantine_marker(DualbootQuarantineState state) noexcept {
    switch(state) {
        case DualbootQuarantineState::Pending: return "URE-DUALBOOT-QUARANTINE-V1\nPENDING\n";
        case DualbootQuarantineState::Committed: return "URE-DUALBOOT-QUARANTINE-V1\nCOMMITTED\n";
        case DualbootQuarantineState::GptRestored: return "URE-DUALBOOT-QUARANTINE-V1\nGPT_RESTORED\n";
        default: return nullptr;
    }
}
inline DualbootQuarantineState dualboot_quarantine_state() noexcept {
    namespace detail=dualboot_quarantine_detail;
    bool missing=false; const int directory=detail::directory(missing);
    if(directory<0)return missing ? DualbootQuarantineState::Clear : DualbootQuarantineState::Invalid;
    struct stat original_directory{},before{},after{},named{};
    const int marker=::openat(directory,detail::phase_name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    bool valid=marker>=0 && detail::private_directory(directory,original_directory) && detail::private_marker(marker,before);
    std::array<char,detail::marker_limit> bytes{}; std::size_t count=0;
    if(valid) {
        const auto length=static_cast<std::size_t>(before.st_size);
        while(count<length) {
            const auto read=::pread(marker,bytes.data()+count,length-count,static_cast<off_t>(count));
            if(read<0 && errno==EINTR)continue;
            if(read<=0) { valid=false; break; }
            count+=static_cast<std::size_t>(read);
        }
        char extra=0; ssize_t remaining;
        do { remaining=::pread(marker,&extra,1,static_cast<off_t>(length)); } while(remaining<0 && errno==EINTR);
        valid=valid && remaining==0 && detail::private_marker(marker,after) && detail::same_file(before,after) &&
            ::fstatat(directory,detail::phase_name,&named,AT_SYMLINK_NOFOLLOW)==0 && detail::same_file(after,named);
    }
    if(marker>=0)::close(marker);
    bool now_missing=false; const int named_directory=detail::directory(now_missing); struct stat current_directory{};
    valid=valid && named_directory>=0 && detail::private_directory(named_directory,current_directory) &&
        current_directory.st_dev==original_directory.st_dev && current_directory.st_ino==original_directory.st_ino;
    if(named_directory>=0)::close(named_directory);
    ::close(directory);
    return valid ? detail::decode(bytes.data(),count) : DualbootQuarantineState::Invalid;
}
inline bool dualboot_quarantine_permits(const char* action) noexcept {
    if(!action || !*action)return false;
    const auto state=dualboot_quarantine_state();
    if(state==DualbootQuarantineState::Clear)return true;
    return (state==DualbootQuarantineState::Committed || state==DualbootQuarantineState::GptRestored) && ::strcmp(action,"reboot")==0;
}
} // namespace ure
