// SPDX-License-Identifier: GPL-3.0-or-later
// Production functions with inert mount/key boundaries; no block devices.
#include <algorithm>
#include <cassert>
#include <iostream>
#include <map>
#include <string>
#include <sys/mount.h>
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
struct Policy { std::string key_raw_ref="fixture-ref"; };
static Policy s_device_policy;
static int s_data_options=1;
static std::map<unsigned,int> s_de_policies{{0,1}}, s_ce_policies;
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
#define retrieveExistingKeyReadOnly read_device_key
#include "systemwide-keys.inc"
#undef retrieveExistingKeyReadOnly
#include "user0-keys.inc"
using userid_t=unsigned;
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
    assert(fscrypt_initialize_systemwide_keys() && installs==1 && de_key_raw_ref=="fixture-ref");
    ro=false; assert(!fscrypt_initialize_systemwide_keys() && installs==1); ro=true;
    key_result=false; assert(!fscrypt_initialize_systemwide_keys() && installs==1); key_result=true;
    assert(fscrypt_init_user0() && fscrypt_init_user0_done);
    for(bool* flag:{&fbe,&exists,&have_de,&loaded}) { *flag=false; assert(!fscrypt_init_user0()); *flag=true; }
    assert(fscrypt_prepare_user_storage("",0,1)); assert(!fscrypt_prepare_user_storage("",0,2));
    s_ce_policies[0]=1; assert(fscrypt_prepare_user_storage("",0,3));
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
