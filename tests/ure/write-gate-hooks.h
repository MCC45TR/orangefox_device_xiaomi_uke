// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <inttypes.h>
#include <deque>
#include <fstream>
#include <functional>
#include <fcntl.h>
#include <map>
#include <memory>
#include <string>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <vector>
#include "ure-write-gate.hpp"

using std::string;
struct GateProbe {
    static inline string fixture;
    static inline std::vector<string> errors, commands, dispatched;
    static inline int lookups=0, side_effects=0, namespace_effects=0, hal=0, data_manager=0, details=0, probes=0;
    static inline bool virtual_ab=false, mounted=false, mounted_read_only=true, statvfs_failure=false;
    struct Mount { string filesystem, options; unsigned long flags; };
    static inline std::vector<Mount> mounts;
    static inline std::deque<int> mount_results;
    static void write_effect() {
        ++side_effects;
        if (fixture.empty()) return;
        std::fstream file(fixture, std::ios::in|std::ios::out|std::ios::binary);
        file.put('!');
    }
    static void reset() {
        errors.clear(); commands.clear(); dispatched.clear(); mounts.clear(); mount_results.clear();
        lookups=side_effects=namespace_effects=hal=data_manager=details=probes=0;
        virtual_ab=mounted=statvfs_failure=false; mounted_read_only=true;
    }
};
struct TestLog {
    template<class T> TestLog& operator<<(const T&) { return *this; }
};
#define LOG(...) TestLog{}
#define PLOG(...) TestLog{}
#define LOGINFO(...) ((void)0)
#define LOGERR(...) ((void)0)
namespace msg { constexpr int kError=1; }
struct Msg {
    explicit Msg(const char*) {}
    Msg(int,const char*) {}
    template<class T> Msg& operator()(const T&) { return *this; }
};
inline void gui_msg(const Msg&) {}
inline void gui_print(const char*,...) {}
struct DataManager {
    static void SetValue(const char*,const string&) { ++GateProbe::data_manager; GateProbe::write_effect(); }
    static void SetValue(const char*,int) { ++GateProbe::data_manager; GateProbe::write_effect(); }
    static int GetIntValue(const char*) { ++GateProbe::probes; return 0; }
};
namespace android::base {
inline string GetProperty(const char*,const char*) { ++GateProbe::probes; return "_a"; }
inline bool GetBoolProperty(const char*,bool) { ++GateProbe::probes; return GateProbe::virtual_ab; }
inline bool StartsWith(const char* input,const char* prefix) { return string(input).starts_with(prefix); }
inline string StringPrintf(const char* format,uint64_t size) {
    char output[32]; std::snprintf(output,sizeof(output),format,size); return output;
}
inline std::vector<string> Split(const char* input,const char*) {
    std::vector<string> result;
    string text(input);
    std::size_t begin=0;
    for (;;) {
        const auto end=text.find(':',begin);
        result.push_back(text.substr(begin,end==string::npos ? end : end-begin));
        if (end==string::npos) return result;
        begin=end+1;
    }
}
}
struct CommandResult { bool success; };
struct BootControlClient {
    static std::shared_ptr<BootControlClient> WaitForService() {
        ++GateProbe::hal; return std::make_shared<BootControlClient>();
    }
    CommandResult SetActiveBootSlot(int32_t) { GateProbe::write_effect(); return {true}; }
};
struct TWFunc {
    static bool Block_Operations_Until_Reboot() { ++GateProbe::probes; return false; }
    static void check_and_run_script(const char*,const char*) { GateProbe::write_effect(); }
    static bool Path_Exists(const string& path) {
        ++GateProbe::probes;
        return path==GateProbe::fixture || path=="/system/bin/exfat-fuse" || path=="/system/bin/ntfs-3g";
    }
    static int copy_file(const string&,const string&,int) { GateProbe::write_effect(); return 0; }
    static int stream_adb_backup(string&);
    static int Exec_Cmd(const string& command) {
        GateProbe::commands.push_back(command);
        if (command.find(" -o ro")==string::npos) GateProbe::write_effect();
        return 0;
    }
    static int Exec_Cmd(const string& command,string&) { return Exec_Cmd(command); }
    static int Exec_Cmd(const string& command,bool) { return Exec_Cmd(command); }
};
struct twrpApex { void Unmount() { GateProbe::write_effect(); } };
constexpr const char* TW_FORMAT_DATA_SCRIPT="/unexecuted-format-script";
class TWPartition {
public:
    bool Mount_Read_Only=true, Can_Be_Mounted=true, Removable=false;
    unsigned int Mount_Flags=0;
    string Current_File_System="ext4", Fstab_File_System="ext4";
    string Actual_Block_Device="fixture-only", Mount_Point="fixture-only", Mount_Options="nosuid,nodev";
    string Symlink_Mount_Point, Symlink_Path;
    bool Mount(bool Display_Error);
    void Change_Mount_Read_Only(bool);
    bool Is_Mounted() { ++GateProbe::probes; return GateProbe::mounted; }
    void Find_Actual_Block_Device() { ++GateProbe::probes; }
    void Check_FS_Type() { ++GateProbe::probes; }
    bool UnMount(bool) { ++GateProbe::namespace_effects; return true; }
    bool Wipe_Encryption() { GateProbe::write_effect(); return true; }
    void Update_Size(bool) { ++GateProbe::probes; }
    bool Bind_Mount(bool) { GateProbe::write_effect(); return true; }
};
class TWPartitionManager {
public:
    string Active_Slot_Display;
    TWPartition data, metadata;
    int Format_Data();
    void Set_Active_Slot(const string&);
    TWPartition* Find_Partition_By_Path(const string& path) {
        ++GateProbe::lookups; return string(path)=="/data" ? &data : &metadata;
    }
    bool Check_Pending_Merges() { GateProbe::write_effect(); return true; }
    void Update_Encryption_Props_Before_Format() { GateProbe::write_effect(); }
    bool Wipe_By_Path(const char*) { GateProbe::write_effect(); return true; }
    bool Fstab_Processed() { return false; }
    void Update_System_Details() { ++GateProbe::details; GateProbe::write_effect(); }
    bool Is_Mounted_By_Path(const string&) { ++GateProbe::probes; return false; }
    bool UnMount_By_Path(const string&,bool,int=0) { ++GateProbe::namespace_effects; return true; }
    void Unlock_Block_Partitions() { GateProbe::write_effect(); }
};
inline TWPartitionManager PartitionManager;
constexpr const char* SCRIPT_FILE_TMP="/unexecuted-ors-script";
class OpenRecoveryScript {
public:
    static int copy_script_file(string);
    static int Run_ORS_File(const string&);
    static int run_script_file() { GateProbe::write_effect(); return 0; }
};
class GUIAction {
public:
    bool simulate=false;
    int dd(string); int cmd(string); int ftls(string);
    void operation_start(const char*) { GateProbe::write_effect(); }
    void operation_end(int) { GateProbe::write_effect(); }
    void simulate_progress_bar() { GateProbe::write_effect(); }
};
struct Repack_Options_struct { bool Disable_Verity, Disable_Force_Encrypt, Backup_First; int Type; };
constexpr int REPLACE_RAMDISK_UNPACKED=3;
class twrpRepacker {
public:
    bool Flash_Current_Twrp();
    bool Repack_Image_And_Flash(const string&,const Repack_Options_struct&) { GateProbe::write_effect(); return true; }
};
inline int ure_test_mount(const char*,const char*,const char* filesystem,unsigned long flags,const void* options) {
    const string data=options ? static_cast<const char*>(options) : "";
    GateProbe::mounts.push_back({filesystem,data,flags});
    const auto required=ure::legacy_read_only_recovery_option(filesystem);
    if (!(flags & MS_RDONLY) || (required[0] && data.find(required)==string::npos)) GateProbe::write_effect();
    if (GateProbe::mount_results.empty()) return 0;
    const int result=GateProbe::mount_results.front(); GateProbe::mount_results.pop_front();
    return result;
}
inline int ure_test_statvfs(const char*,struct statvfs* info) {
    info->f_flag=GateProbe::mounted_read_only ? ST_RDONLY : 0;
    return GateProbe::statvfs_failure ? -1 : 0;
}
inline int ure_test_chmod(const char*,mode_t) { GateProbe::write_effect(); return 0; }
inline int ure_test_unlink(const char*) { ++GateProbe::namespace_effects; GateProbe::write_effect(); return 0; }

constexpr int FB_RESPONSE_SZ=64;
constexpr const char* FB_CMD_OEM="oem";
enum class FastbootResult { FAIL, OKAY };
struct FakeTransport {
    std::deque<string> input;
    int invalid_result=-1;
    int Read(char* buffer,int max) {
        if (input.empty()) return invalid_result;
        const auto command=input.front(); input.pop_front();
        const auto size=std::min(command.size(),static_cast<std::size_t>(max));
        std::memcpy(buffer,command.data(),size);
        return static_cast<int>(size);
    }
};
class FastbootDevice {
public:
    using Handler=std::function<bool(FastbootDevice*,const std::vector<string>&)>;
    std::map<string,Handler> kCommandMap;
    std::unique_ptr<FakeTransport> transport_=std::make_unique<FakeTransport>();
    std::vector<string> failures;
    bool output_ok=true;
    void ExecuteCommands();
    bool WriteStatus(FastbootResult,const string& text) { failures.push_back(text); return output_ok; }
    bool WriteFail(const string& text) { return WriteStatus(FastbootResult::FAIL,text); }
};
struct PartitionHandle {
    int opens=0;
    int fd() const { return 7; }
    bool Open(int flags) {
        ++opens;
        if ((flags & O_ACCMODE)!=O_RDONLY || (flags & (O_CREAT|O_TRUNC|O_APPEND))) GateProbe::write_effect();
        return true;
    }
};
inline bool LogicalPartitionExists(FastbootDevice*,const string& name,bool* zero=nullptr) {
    ++GateProbe::lookups;
    if (zero) *zero=name=="zero-fixture";
    return name=="logical-fixture" || name=="zero-fixture";
}
inline bool OpenLogicalPartition(FastbootDevice*,const string&,PartitionHandle*) { ++GateProbe::lookups; return true; }
inline bool OpenPhysicalPartition(const string&,PartitionHandle*) { ++GateProbe::lookups; return true; }
bool OpenPartition(FastbootDevice*,const string&,PartitionHandle*,int flags=O_WRONLY);
inline uint64_t get_block_device_size(int) { ++GateProbe::probes; return 4096; }
bool GetPartitionSize(FastbootDevice*,const std::vector<string>&,string*);
