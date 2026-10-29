// SPDX-License-Identifier: GPL-3.0-or-later
// Production functions with inert mount/key boundaries; no block devices.
#include <algorithm>
#include <cassert>
#include <iostream>
#include <cstdarg>
#include <fcntl.h>
#include <linux/fscrypt.h>
#include <map>
#include <string>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <vector>
#define LOG(level) std::clog
#define LOGERR(...) ((void)0)
struct Entry { unsigned long flags=MS_RDONLY; std::string fs_type="f2fs", fs_options="norecovery,inlinecrypt"; };
static Entry entry;
static bool have_entry=true, mount_result=true, key_result=true;
static unsigned mounts=0, reads=0, installs=0;
static std::vector<int> fstab_default;
static Entry* GetEntryForMountPoint(std::vector<int>*, const std::string&) { return have_entry ? &entry : nullptr; }
namespace android::base {
std::vector<std::string> Split(const std::string& input,const std::string&) {
    std::vector<std::string> parts;
    for(std::size_t begin=0;;) { const auto end=input.find(',',begin); parts.push_back(input.substr(begin,end-begin)); if(end==std::string::npos)break; begin=end+1; }
    return parts;
}
}
extern "C" int __wrap_mount(const char* source,const char* path,const char* type,unsigned long flags,const void*) {
    assert(std::string(source)=="/dev/block/dm-fixture" && std::string(path)=="/data");
    assert(std::string(type)==entry.fs_type && (flags&MS_RDONLY) && (flags&MS_NOSUID) && (flags&MS_NODEV) && (flags&MS_NOEXEC));
    ++mounts; return mount_result ? 0 : -1;
}
#include "mount-metadata.inc"
struct KeyGeneration {};
using KeyBuffer=std::vector<unsigned char>;
struct Authentication {};
static Authentication kEmptyAuthentication;
static bool retrieveExistingKeyReadOnly(const std::string& path,const Authentication&,KeyBuffer*) {
    assert(path=="/metadata/vold/metadata_encryption/key"); ++reads; return key_result;
}
#include "read-metadata-key.inc"
static bool ro=true, fbe=true, exists=true, have_de=true, loaded=true;
static const char* DATA_MNT_POINT="/data";
static std::string device_key_path="/data/unencrypted/key", de_key_raw_ref;
static std::string data_data_dir="/data/data";
struct Options { int version=2,contents_mode=1,filenames_mode=4,flags=2; bool dusize_4k=false; };
struct EncryptionPolicy { Options options; std::string key_raw_ref=std::string(16,'k'); };
using Policy=EncryptionPolicy;
struct UserPolicies { EncryptionPolicy internal; };
static Policy s_device_policy;
static int s_data_options=1;
static std::map<unsigned,UserPolicies> s_de_policies{{0,{}}}, s_ce_policies;
static bool fscrypt_init_user0_done=false;
static bool init_data_file_encryption_options() { return true; }
static bool IsFbeEnabled() { return fbe; }
static bool de_key_exists(unsigned) { return have_de; }
static bool ce_key_exists(unsigned) { return exists; }
static bool load_all_de_keys() { return loaded; }
static bool install_storage_key(const char*,int,const KeyBuffer&,Policy*) { ++installs; return true; }
static bool read_device_key(const std::string& path,const Authentication&,KeyBuffer*) {
    assert(path==device_key_path); ++reads; return key_result;
}
extern "C" int __wrap_statvfs(const char* path,struct statvfs* fs) {
    assert(std::string(path)=="/data"); fs->f_flag=ro ? ST_RDONLY : 0; return 0;
}
namespace android::os { struct IVold { enum {STORAGE_FLAG_DE=1,STORAGE_FLAG_CE=2}; }; }
namespace android::base { struct unique_fd { int fd; explicit unique_fd(int value):fd(value) {} int get()const{return fd;} }; }
namespace android::vold {
static std::string BuildDataSystemDePath(unsigned id) { return "/data/system_de/"+std::to_string(id); }
static std::string BuildDataVendorDePath(unsigned id) { return "/data/vendor_de/"+std::to_string(id); }
static std::string BuildDataMiscDePath(const std::string&,unsigned id) { return "/data/misc_de/"+std::to_string(id); }
static std::string BuildDataUserDePath(const std::string&,unsigned id) { return "/data/user_de/"+std::to_string(id); }
static std::string BuildDataSystemCePath(unsigned id) { return "/data/system_ce/"+std::to_string(id); }
static std::string BuildDataVendorCePath(unsigned id) { return "/data/vendor_ce/"+std::to_string(id); }
static std::string BuildDataMiscCePath(const std::string&,unsigned id) { return "/data/misc_ce/"+std::to_string(id); }
static std::string BuildDataMediaCePath(const std::string&,unsigned id) { return "/data/media/"+std::to_string(id); }
static std::string BuildDataUserCePath(const std::string&,unsigned id) { return "/data/user/"+std::to_string(id); }
}
static fscrypt_get_policy_ex_arg kernel_policy;
static bool policy_readable=true, same_device=true;
static unsigned policy_reads=0;
extern "C" int __wrap_open(const char* path,int flags,...) {
    assert(std::string(path).starts_with("/data/") && (flags&O_ACCMODE)==O_RDONLY && (flags&O_NOFOLLOW) && (flags&O_DIRECTORY));
    return 100;
}
extern "C" int __wrap_fstat(int fd,struct stat* info) { assert(fd==100); info->st_dev=same_device?7:8; return 0; }
extern "C" int __wrap_stat(const char* path,struct stat* info) { assert(std::string(path)=="/data"); info->st_dev=7; return 0; }
extern "C" int __wrap_fstatvfs(int fd,struct statvfs* info) { assert(fd==100); info->f_flag=ro?ST_RDONLY:0; info->f_bsize=4096; return 0; }
extern "C" int __wrap_ioctl(int fd,unsigned long request,...) {
    assert(fd==100 && request==FS_IOC_GET_ENCRYPTION_POLICY_EX); ++policy_reads;
    va_list args; va_start(args,request); auto* result=va_arg(args,fscrypt_get_policy_ex_arg*); va_end(args);
    *result=kernel_policy; return policy_readable?0:-1;
}
#define retrieveExistingKeyReadOnly read_device_key
#include "systemwide-keys.inc"
#undef retrieveExistingKeyReadOnly
#include "user0-keys.inc"
using userid_t=unsigned;
#include "verify-policy.inc"
#include "prepare-user-storage.inc"
namespace ure::readonly_fstab {
static bool mounted_readonly(const char*,const std::string&) { return ro; }
}
struct Partition {
    bool Mount_Read_Only=true, mountable=true;
    std::string Actual_Block_Device="/dev/block/fixture";
    bool Mount(bool) { ++mounts; return mountable; }
};
class TWPartitionManager {
public:
    Partition data, metadata;
    bool present=true;
    Partition* Find_Partition_By_Path(const std::string& path) { return present ? (path=="/data" ? &data : &metadata) : nullptr; }
    bool Read_Only_Android_Access(bool require_data);
};
static bool additional_fstab_readonly_approved=true;
#include "android-access.inc"
int main() {
    assert(mount_via_fs_mgr("/data","/dev/block/dm-fixture",false) && mounts==1);
    entry.fs_options="ro"; assert(!mount_via_fs_mgr("/data","/dev/block/dm-fixture",false) && mounts==1);
    entry.fs_type="ext4"; entry.fs_options="noload"; assert(mount_via_fs_mgr("/data","/dev/block/dm-fixture",false));
    entry.flags=0; assert(!mount_via_fs_mgr("/data","/dev/block/dm-fixture",false)); entry.flags=MS_RDONLY;
    entry.fs_type="auto"; assert(!mount_via_fs_mgr("/data","/dev/block/dm-fixture",false));
    assert(!mount_via_fs_mgr("/data","/dev/block/dm-fixture",true));
    assert(!mount_via_fs_mgr("/metadata","/dev/block/dm-fixture",false));
    have_entry=false; assert(!mount_via_fs_mgr("/data","/dev/block/dm-fixture",false)); have_entry=true;
    entry.fs_type="ext4"; mount_result=false; assert(!mount_via_fs_mgr("/data","/dev/block/dm-fixture",false));
    KeyBuffer key;
    assert(!read_key("/metadata/other",{},true,&key) && reads==0);
    assert(read_key("/metadata/vold/metadata_encryption",{},true,&key) && reads==1);
    assert(fscrypt_initialize_systemwide_keys() && installs==1 && de_key_raw_ref==std::string(16,'k'));
    ro=false; assert(!fscrypt_initialize_systemwide_keys() && installs==1); ro=true;
    key_result=false; assert(!fscrypt_initialize_systemwide_keys() && installs==1); key_result=true;
    assert(fscrypt_init_user0() && fscrypt_init_user0_done);
    for(bool* flag:{&fbe,&exists,&have_de,&loaded}) { *flag=false; assert(!fscrypt_init_user0()); *flag=true; }
    kernel_policy.policy_size=sizeof(kernel_policy.policy.v2);
    kernel_policy.policy.v2.version=FSCRYPT_POLICY_V2;
    kernel_policy.policy.v2.contents_encryption_mode=1; kernel_policy.policy.v2.filenames_encryption_mode=4;
    kernel_policy.policy.v2.flags=2;
    std::fill_n(kernel_policy.policy.v2.master_key_identifier,16,'k');
    assert(fscrypt_prepare_user_storage("",0,1) && policy_reads==4); assert(!fscrypt_prepare_user_storage("",0,2));
    s_ce_policies[0]={}; assert(fscrypt_prepare_user_storage("",0,3));
    kernel_policy.policy.v2.master_key_identifier[0]='x'; assert(!fscrypt_prepare_user_storage("",0,3));
    kernel_policy.policy.v2.master_key_identifier[0]='k';
    kernel_policy.policy.v2.flags=0; assert(!fscrypt_prepare_user_storage("",0,3)); kernel_policy.policy.v2.flags=2;
    kernel_policy.policy.v2.log2_data_unit_size=12; assert(!fscrypt_prepare_user_storage("",0,3)); kernel_policy.policy.v2.log2_data_unit_size=0;
    policy_readable=false; assert(!fscrypt_prepare_user_storage("",0,3)); policy_readable=true;
    same_device=false; assert(!fscrypt_prepare_user_storage("",0,3)); same_device=true;
    EncryptionPolicy legacy; legacy.options.version=1; legacy.key_raw_ref=std::string(8,'l');
    const auto v2=kernel_policy; kernel_policy={}; kernel_policy.policy_size=sizeof(kernel_policy.policy.v1);
    kernel_policy.policy.v1.version=FSCRYPT_POLICY_V1; kernel_policy.policy.v1.contents_encryption_mode=1;
    kernel_policy.policy.v1.filenames_encryption_mode=4; kernel_policy.policy.v1.flags=2;
    std::fill_n(kernel_policy.policy.v1.master_key_descriptor,8,'l');
    assert(verify_existing_policy(legacy,"/data/legacy")); legacy.key_raw_ref[0]='x';
    assert(!verify_existing_policy(legacy,"/data/legacy")); kernel_policy=v2;
    assert(!fscrypt_prepare_user_storage("external",0,3)); assert(!fscrypt_prepare_user_storage("",0,4));
    assert(!fscrypt_prepare_user_storage("",1,3)); assert(!fscrypt_prepare_user_storage("",0,0));
    ro=false; assert(!fscrypt_prepare_user_storage("",0,3)); ro=true;
    TWPartitionManager manager; assert(manager.Read_Only_Android_Access(true));
    const auto before=mounts; additional_fstab_readonly_approved=false;
    assert(!manager.Read_Only_Android_Access(true) && mounts==before); additional_fstab_readonly_approved=true;
    manager.present=false; assert(!manager.Read_Only_Android_Access(false)); manager.present=true;
    manager.data.Mount_Read_Only=false; assert(!manager.Read_Only_Android_Access(false)); manager.data.Mount_Read_Only=true;
    manager.metadata.mountable=false; assert(!manager.Read_Only_Android_Access(false)); manager.metadata.mountable=true;
    ro=false; assert(!manager.Read_Only_Android_Access(false)); ro=true;
    manager.data.mountable=false; assert(manager.Read_Only_Android_Access(false) && !manager.Read_Only_Android_Access(true));
    std::cout<<"PASS actual Android profile/mount admission, no-replay metadata mount, existing-key initialization and internal DE/CE preparation refusals; host boundaries only.\n";
}
