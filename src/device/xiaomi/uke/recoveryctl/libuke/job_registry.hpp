// SPDX-License-Identifier: Apache-2.0
#pragma once
// Runtime activity exclusion is separate from durable storage ownership.
// This header also serves stock recovery and fastbootd without JsonCpp,
// libcrypto, exception support or a dependency on the GUI executor.
#include <array>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <mutex>
#include <utility>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ure {
namespace job_registry_detail {
constexpr std::size_t path_limit=4096,job_limit=64;
struct Entry { std::array<char,33> id{}; pid_t process=0; bool used=false,lifecycle=false; };
struct State {
    std::mutex mutex;
    std::array<Entry,job_limit> entries{};
    std::array<char,path_limit> path{};
    dev_t directory_device=0,lock_device=0;
    ino_t directory_inode=0,lock_inode=0;
    bool frozen=false;
};
inline State& state() { static State value; return value; }
inline const char* runtime_path() noexcept {
#if defined(__ANDROID__) && !defined(URE_HOST_POLICY_FIXTURE)
    // No environment variable can replace the shipping runtime domain.
    return "/tmp/ure-job-registry";
#else
    return ::getenv("URE_GUI_JOB_REGISTRY");
#endif
}
inline bool valid_id(const char* id) noexcept {
    if(!id || ::strnlen(id,33)!=32)return false;
    for(std::size_t i=0;i<32;++i)if(!((id[i]>='0' && id[i]<='9') || (id[i]>='a' && id[i]<='f')))return false;
    return true;
}
inline bool private_identity(int fd,bool directory,struct stat& observed) noexcept {
    return ::fstat(fd,&observed)==0 && (directory ? S_ISDIR(observed.st_mode) : S_ISREG(observed.st_mode)) &&
        observed.st_uid==::geteuid() && (observed.st_mode&07777)==(directory ? 0700 : 0600) &&
        (directory || (observed.st_nlink==1 && observed.st_size==0));
}
inline bool same_identity(const struct stat& observed,dev_t device,ino_t inode) noexcept {
    return observed.st_dev==device && observed.st_ino==inode;
}
// Walk every component without following symlinks; create only the final
// private directory. This runtime lock makes no persistent-durability claim.
inline int directory(const char* path,bool create) noexcept {
    if(!path || path[0]!='/' || ::strnlen(path,path_limit)>=path_limit)return -1;
    const auto length=::strlen(path); if(length<2 || path[length-1]=='/')return -1;
    int current=::open("/",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(current<0)return -1;
    const char* first=path+1;
    while(*first) {
        const char* end=::strchr(first,'/'); const auto count=end ? static_cast<std::size_t>(end-first) : ::strlen(first);
        std::array<char,256> part{};
        if(count==0 || count>=part.size() || (count==1 && first[0]=='.') || (count==2 && first[0]=='.' && first[1]=='.')) {
            ::close(current); return -1;
        }
        for(std::size_t i=0;i<count;++i)if(static_cast<unsigned char>(first[i])<32) { ::close(current); return -1; }
        ::memcpy(part.data(),first,count);
        if(!end && create && ::mkdirat(current,part.data(),0700)!=0 && errno!=EEXIST) { ::close(current); return -1; }
        const int next=::openat(current,part.data(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        ::close(current); if(next<0)return -1; current=next;
        if(!end)break;
        first=end+1;
    }
    struct stat observed{};
    if(!private_identity(current,true,observed)) { ::close(current); return -1; }
    return current;
}
} // namespace job_registry_detail

class RuntimeActivityLease {
    int directory_=-1,lock_=-1;
    pid_t process_=0;
    std::size_t slot_=job_registry_detail::job_limit;
    const char* error_="gui-registry-inactive";
    void release() noexcept {
        // Closing a fork-inherited descriptor cannot unlock the parent's
        // open file description while the parent still retains its own FD.
        if(slot_<job_registry_detail::job_limit && process_==::getpid()) {
            auto& registry=job_registry_detail::state(); std::lock_guard<std::mutex> held(registry.mutex);
            registry.entries[slot_]=job_registry_detail::Entry{};
        }
        if(lock_>=0)::close(lock_);
        if(directory_>=0)::close(directory_);
        directory_=-1; lock_=-1; slot_=job_registry_detail::job_limit;
    }
public:
    RuntimeActivityLease()=default;
    ~RuntimeActivityLease() { release(); }
    RuntimeActivityLease(const RuntimeActivityLease&)=delete;
    RuntimeActivityLease& operator=(const RuntimeActivityLease&)=delete;
    RuntimeActivityLease(RuntimeActivityLease&& other) noexcept { *this=std::move(other); }
    RuntimeActivityLease& operator=(RuntimeActivityLease&& other) noexcept {
        if(this!=&other) {
            release(); directory_=other.directory_; lock_=other.lock_; process_=other.process_; slot_=other.slot_; error_=other.error_;
            other.directory_=-1; other.lock_=-1; other.slot_=job_registry_detail::job_limit;
        }
        return *this;
    }
    const char* error() const noexcept { return error_; }
    bool acquired() const noexcept { return lock_>=0 && slot_<job_registry_detail::job_limit && process_==::getpid(); }
    static RuntimeActivityLease acquire(const char* job_id,bool lifecycle=false) noexcept {
        RuntimeActivityLease result;
        if(!lifecycle && !job_registry_detail::valid_id(job_id)) { result.error_="invalid-gui-job-identity"; return result; }
        const auto* path=job_registry_detail::runtime_path();
        if(!path || ::strnlen(path,job_registry_detail::path_limit)>=job_registry_detail::path_limit) { result.error_="gui-registry-unavailable"; return result; }
        {
            auto& registry=job_registry_detail::state(); std::lock_guard<std::mutex> held(registry.mutex);
            if(registry.frozen && ::strcmp(path,registry.path.data())!=0) { result.error_="gui-registry-domain-changed"; return result; }
        }
        result.directory_=job_registry_detail::directory(path,true);
        if(result.directory_<0) { result.error_="gui-registry-unavailable"; return result; }
        result.lock_=::openat(result.directory_,"activity.lock",O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK,0600);
        struct stat directory{},lock{};
        if(result.lock_<0 || !job_registry_detail::private_identity(result.directory_,true,directory) ||
           !job_registry_detail::private_identity(result.lock_,false,lock)) { result.error_="unsafe-gui-registry"; return result; }
        if(::flock(result.lock_,(lifecycle ? LOCK_EX : LOCK_SH)|LOCK_NB)!=0) {
            result.error_=(errno==EWOULDBLOCK || errno==EAGAIN) ? "gui-lifecycle-busy" : "gui-registry-unavailable"; return result;
        }
        auto& registry=job_registry_detail::state(); std::lock_guard<std::mutex> held(registry.mutex);
        if(registry.frozen && (::strcmp(path,registry.path.data())!=0 ||
           !job_registry_detail::same_identity(directory,registry.directory_device,registry.directory_inode) ||
           !job_registry_detail::same_identity(lock,registry.lock_device,registry.lock_inode))) {
            result.error_="gui-registry-domain-changed"; return result;
        }
        if(!registry.frozen) {
            ::memcpy(registry.path.data(),path,::strlen(path)+1);
            registry.directory_device=directory.st_dev; registry.directory_inode=directory.st_ino;
            registry.lock_device=lock.st_dev; registry.lock_inode=lock.st_ino; registry.frozen=true;
        }
        if(!lifecycle)for(const auto& entry:registry.entries)if(entry.used && entry.process==::getpid() && !entry.lifecycle && ::strcmp(entry.id.data(),job_id)==0) {
            result.error_="gui-registry-job-mismatch"; return result;
        }
        for(std::size_t i=0;i<registry.entries.size();++i)if(!registry.entries[i].used || registry.entries[i].process!=::getpid()) {
            registry.entries[i]=job_registry_detail::Entry{};
            registry.entries[i].used=true; registry.entries[i].lifecycle=lifecycle;
            registry.entries[i].process=::getpid();
            if(!lifecycle)::memcpy(registry.entries[i].id.data(),job_id,32);
            result.slot_=i; result.process_=::getpid(); result.error_=""; return result;
        }
        result.error_="gui-registry-job-limit"; return result;
    }
    bool valid() const noexcept {
        if(!acquired())return false;
        const auto* path=job_registry_detail::runtime_path();
        {
            auto& registry=job_registry_detail::state(); std::lock_guard<std::mutex> held(registry.mutex);
            if(!path || ::strcmp(path,registry.path.data())!=0)return false;
        }
        const int selected=job_registry_detail::directory(path,false); if(selected<0)return false;
        struct stat directory{},lock{},named_lock{};
        const bool identities=job_registry_detail::private_identity(selected,true,directory) &&
            job_registry_detail::private_identity(lock_,false,lock) &&
            ::fstatat(selected,"activity.lock",&named_lock,AT_SYMLINK_NOFOLLOW)==0;
        ::close(selected);
        if(!identities || !S_ISREG(named_lock.st_mode) || named_lock.st_dev!=lock.st_dev || named_lock.st_ino!=lock.st_ino)return false;
        auto& registry=job_registry_detail::state(); std::lock_guard<std::mutex> held(registry.mutex);
        return registry.entries[slot_].used && registry.entries[slot_].process==process_ && ::strcmp(path,registry.path.data())==0 &&
            job_registry_detail::same_identity(directory,registry.directory_device,registry.directory_inode) &&
            job_registry_detail::same_identity(lock,registry.lock_device,registry.lock_inode);
    }
};
inline std::size_t runtime_active_jobs() noexcept {
    auto& registry=job_registry_detail::state(); std::lock_guard<std::mutex> held(registry.mutex);
    std::size_t count=0; for(const auto& entry:registry.entries)if(entry.used && entry.process==::getpid() && !entry.lifecycle)++count; return count;
}
inline void initialize_runtime_registry_lifetime() noexcept { static_cast<void>(job_registry_detail::state()); }

#ifdef __ANDROID__
namespace job_registry_detail {
struct RebootState { std::mutex mutex; RuntimeActivityLease staged; };
inline RebootState& reboot_state() { initialize_runtime_registry_lifetime(); static RebootState value; return value; }
}
inline bool runtime_stage_reboot() noexcept {
    auto& reboot=job_registry_detail::reboot_state(); std::lock_guard<std::mutex> held(reboot.mutex);
    if(reboot.staged.acquired())return reboot.staged.valid();
    reboot.staged=RuntimeActivityLease::acquire(nullptr,true); return reboot.staged.valid();
}
inline RuntimeActivityLease runtime_lifecycle_acquire(bool adopt_staged_reboot) noexcept {
    if(adopt_staged_reboot) {
        auto& reboot=job_registry_detail::reboot_state(); std::lock_guard<std::mutex> held(reboot.mutex);
        if(reboot.staged.acquired())return std::move(reboot.staged);
    }
    return RuntimeActivityLease::acquire(nullptr,true);
}
#endif
} // namespace ure
