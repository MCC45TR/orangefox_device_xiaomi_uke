#include "fake.hpp"
#include "ExistingKeyMint.h"
#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace io_fixture {
inline int directory=-1, opens=0, reads=0;
inline bool directory_readonly=true, file_readonly=true, directory_owner=true, file_owner=true;
inline bool directory_stat_error=false, statvfs_error=false, pending_error=false;
inline int interrupted=0, read_error=0, changed_field=0;
inline size_t maximum_read=65536;
inline std::string fault_name="keymaster_key_blob";
inline std::map<int,std::string> names;
inline std::map<int,int> stats;

inline void reset(int fd) {
    directory=fd; opens=0; reads=0;
    directory_readonly=file_readonly=directory_owner=file_owner=true;
    directory_stat_error=statvfs_error=pending_error=false;
    interrupted=read_error=changed_field=0; maximum_read=65536;
    fault_name="keymaster_key_blob"; names.clear(); stats.clear();
}
inline int openat(int fd,const char* name,int flags) {
    assert(fd==directory && (flags&O_ACCMODE)==O_RDONLY && (flags&O_NOFOLLOW) &&
           (flags&O_CLOEXEC) && (flags&O_NONBLOCK));
    ++opens;
    const int result=::openat(fd,name,flags);
    if(result>=0) { names[result]=name; stats[result]=0; }
    return result;
}
inline int fstat(int fd,struct stat* status) {
    if(fd==directory && directory_stat_error) { errno=EIO; return -1; }
    const int result=::fstat(fd,status);
    if(result!=0) return result;
    // Host temporary files belong to the test user. Only UID and read-only policy are modeled.
    status->st_uid=(fd==directory ? directory_owner : file_owner) ? 0 : 1001;
    if(fd!=directory && names[fd]==fault_name && ++stats[fd]>1) {
        if(changed_field==1) ++status->st_ino;
        if(changed_field==2) ++status->st_size;
        if(changed_field==3) ++status->st_mtim.tv_nsec;
        if(changed_field==4) ++status->st_ctim.tv_sec;
    }
    return result;
}
inline int fstatvfs(int fd,struct statvfs* status) {
    if(statvfs_error) { errno=EIO; return -1; }
    const int result=::fstatvfs(fd,status);
    if(result==0) {
        if(fd==directory ? directory_readonly : file_readonly) status->f_flag|=ST_RDONLY;
        else status->f_flag&=~ST_RDONLY;
    }
    return result;
}
inline int fstatat(int fd,const char* name,struct stat* status,int flags) {
    assert(flags==AT_SYMLINK_NOFOLLOW);
    if(pending_error) { errno=EACCES; return -1; }
    return ::fstatat(fd,name,status,flags);
}
inline ssize_t pread(int fd,void* data,size_t size,off_t offset) {
    ++reads;
    if(names[fd]==fault_name) {
        if(interrupted>0) { --interrupted; errno=EINTR; return -1; }
        if(read_error) { errno=EIO; return read_error==1 ? -1 : 0; }
        size=std::min(size,maximum_read);
    }
    return ::pread(fd,data,size,offset);
}
}  // namespace io_fixture

namespace android::base {
class unique_fd {
    int fd_;
  public:
    explicit unique_fd(int fd):fd_(fd) {}
    ~unique_fd() { if(fd_>=0) ::close(fd_); }
    int get() const { return fd_; }
};
}

namespace android::vold {
constexpr size_t SECDISCARDABLE_BYTES=16384, GCM_NONCE_BYTES=12, GCM_MAC_BYTES=16;
const char* kCurrentVersion="1";
const char* kFn_version="version";
const char* kFn_secdiscardable="secdiscardable";
const char* kFn_encrypted_key="encrypted_key";
const char* kFn_keymaster_key_blob="keymaster_key_blob";
const char* kFn_keymaster_key_blob_upgraded="keymaster_key_blob_upgraded";
const char* kHashPrefix_secdiscardable="Android secdiscardable SHA512";
enum class MetadataKeyBinding { Unknown, Unbound, Bound };
struct StorageBindingInfo {
    enum class State { UNINITIALIZED, IN_USE, NOT_USED };
    std::vector<uint8_t> seed;
    State state=State::UNINITIALIZED;
    std::mutex guard;
};
StorageBindingInfo storage_binding_info;
// File-reader control only: cryptographic SHA512 correctness is not under test here.
inline void hashWithPrefix(const char* prefix,const std::string& data,std::string* output) {
    assert(std::string(prefix)==kHashPrefix_secdiscardable && data.size()==16384);
    output->assign(64,'h');
}
}

#define openat io_fixture::openat
#define fstat io_fixture::fstat
#define fstatvfs io_fixture::fstatvfs
#define fstatat io_fixture::fstatat
#define pread io_fixture::pread
