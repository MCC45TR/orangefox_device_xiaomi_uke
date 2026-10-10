// SPDX-License-Identifier: Apache-2.0
#pragma once
// Only the coordinator touches disposable host files. Every lifecycle effect
// in the extracted production hooks is redirected to the ledger below.
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <unistd.h>
#include <vector>
#include "ure-lifecycle.hpp"

using std::string;
struct LifecycleProbe {
    struct Unmount { string path; int flags; };
    static inline std::vector<string> effects, errors, failures, status;
    static inline std::vector<Unmount> unmounts;
    static inline std::map<string,bool> mounted;
    static inline std::map<string,string> values;
    static inline std::deque<int> unmount_results;
    static inline std::function<void()> during_effect;
    static inline unsigned probes=0, data_reads=0;
    static inline int never_unmount_system=0;
    static inline bool lazy_unmount_succeeds=true, ensure_unmount_succeeds=true;
    static inline const char* system_boot_error=nullptr;
    static void effect(string name) {
        effects.push_back(std::move(name));
        if(during_effect)during_effect();
    }
    template<class... Args> static void error(const char* format,Args... args) {
        char message[256]; std::snprintf(message,sizeof(message),format,args...); errors.emplace_back(message);
    }
    static void reset() {
        effects.clear(); errors.clear(); failures.clear(); status.clear(); unmounts.clear(); values.clear(); unmount_results.clear();
        during_effect={}; probes=data_reads=0; never_unmount_system=0;
        lazy_unmount_succeeds=ensure_unmount_succeeds=true;
        system_boot_error=nullptr;
        mounted={{"/data",true},{"/data-child",true},{"/system",true},{"/sdcard1",true},{"/sdcard",true},{"/data/media",true}};
    }
};
namespace ure {
inline const char* prepare_system_boot(const LegacyLifecycleGuard& lifecycle) noexcept {
    LifecycleProbe::effect("system-boot.prepare");
    return lifecycle.active() ? LifecycleProbe::system_boot_error : "system-boot-operation-pending";
}
}
struct LifecycleLog {
    template<class T> LifecycleLog& operator<<(const T&) { return *this; }
};
#define LOG(...) LifecycleLog{}
#define LOGINFO(...) ((void)0)
#define LOGERR(...) LifecycleProbe::error(__VA_ARGS__)
namespace msg { constexpr int kError=1; }
struct Msg {
    Msg(int,const char*) {}
    template<class T> Msg& operator()(const T&) { return *this; }
};
inline void gui_msg(const Msg&) { LifecycleProbe::errors.emplace_back("partition-unmount-refusal"); }
constexpr const char* TW_DONT_UNMOUNT_SYSTEM="tw_dont_unmount_system";
constexpr const char* FOX_DISABLE_DM_VERITY="fox_disable_dm_verity";
constexpr const char* FOX_DISABLE_FORCED_ENCRYPTION="fox_disable_forced_encryption";
inline int Fox_AutoDeactivate_OnReboot=0;
inline int Fox_IsDeactivation_Process_Called=0;
#define ANDROID_RB_PROPERTY "sys.powerctl"
#define OF_UNMOUNT_SDCARDS_BEFORE_REBOOT 1

struct DataManager {
    static void GetValue(const char*,int& value) { ++LifecycleProbe::data_reads; value=LifecycleProbe::never_unmount_system; }
    static int GetIntValue(const char*) { ++LifecycleProbe::data_reads; return 0; }
    static void SetValue(const char* key,const string& value) { LifecycleProbe::effect("data.set:"+string(key)); LifecycleProbe::values[key]=value; }
    static void SetValue(const char* key,int value) { SetValue(key,std::to_string(value)); }
    static void Flush() { LifecycleProbe::effect("data.flush"); }
};
enum RebootCommand { rb_current=0,rb_system,rb_recovery,rb_poweroff,rb_bootloader,rb_download,rb_edl,rb_fastboot };
struct TWFunc {
    static int tw_reboot(RebootCommand);
    static string Get_Root_Path(const string& path) {
        ++LifecycleProbe::probes;
        const auto slash=path.find('/',1); return path.substr(0,slash);
    }
    static void Update_Log_File() { LifecycleProbe::effect("log.update"); }
    static void Run_Before_Reboot() { LifecycleProbe::effect("reboot.before"); }
    static void Deactivation_Process() { LifecycleProbe::effect("deactivation.callback"); }
    static void Update_Intent_File(const char*) { LifecycleProbe::effect("reboot.intent"); }
    static void check_and_run_script(const char* path,const char*) { LifecycleProbe::effect("script.callback:"+string(path)); }
    static int Exec_Cmd(const string& command) {
        LifecycleProbe::effect("command.callback:"+command);
        constexpr const char* lazy="umount -l "; constexpr const char* storage="/system/bin/umount ";
        if(command.starts_with(lazy) && LifecycleProbe::lazy_unmount_succeeds)LifecycleProbe::mounted[command.substr(std::strlen(lazy))]=false;
        if(command.starts_with(storage))LifecycleProbe::mounted[command.substr(std::strlen(storage))]=false;
        return 0;
    }
};
struct TWPartition {
    string Mount_Point="/data",Symlink_Mount_Point,SubPartition_Of;
    bool Is_Storage=false,Is_SubPartition=false;
    unsigned MTP_Storage_ID=0;
    bool Is_Mounted() { ++LifecycleProbe::probes; return LifecycleProbe::mounted[Mount_Point]; }
    bool UnMount(bool,int=0,const ure::LegacyLifecycleGuard* parent=nullptr);
};
struct TWPartitionManager {
    std::vector<TWPartition*> Partitions;
    int UnMount_By_Path(string,bool,int=0,const ure::LegacyLifecycleGuard* parent=nullptr);
    string Get_Android_Root_Path() { ++LifecycleProbe::probes; return "/system"; }
    void Remove_MTP_Storage(unsigned) { LifecycleProbe::effect("mtp.remove"); }
    TWPartition* Find_Partition_By_Path(const string& path) {
        ++LifecycleProbe::probes;
        for(auto* partition:Partitions)if(partition->Mount_Point==path)return partition;
        return nullptr;
    }
    bool Is_Mounted_By_Path(const string& path) { ++LifecycleProbe::probes; return LifecycleProbe::mounted[path]; }
};
inline TWPartitionManager PartitionManager;
struct GUIAction { int reboot(string); };
inline void ure_test_sync() { LifecycleProbe::effect("sync.callback"); }
inline int ure_test_umount2(const char* path,int flags) {
    LifecycleProbe::effect("unmount.callback:"+string(path)); LifecycleProbe::unmounts.push_back({path,flags});
    int result=0;
    if(!LifecycleProbe::unmount_results.empty()) { result=LifecycleProbe::unmount_results.front(); LifecycleProbe::unmount_results.pop_front(); }
    if(result==0)LifecycleProbe::mounted[path]=false; else errno=EBUSY;
    return result;
}
inline unsigned ure_test_sleep(unsigned) { LifecycleProbe::effect("sleep.callback"); return 0; }
inline int ure_test_usleep(useconds_t) { LifecycleProbe::effect("usleep.callback"); return 0; }
inline int ure_test_pause() { LifecycleProbe::effect("pause.callback"); return 0; }
inline int ure_test_reboot(int) { LifecycleProbe::effect("reboot.syscall.callback"); return 0; }
inline int ure_test_android_reboot(int,int,const char*) { LifecycleProbe::effect("android-reboot.callback"); return 0; }
inline int property_set(const char* key,const char* value) { LifecycleProbe::effect("property.callback:"+string(key)+"="+value); return 0; }
namespace android::base {
inline bool SetProperty(const char* key,const char* value) { static_cast<void>(property_set(key,value)); return true; }
}
inline int fstab=0;
namespace android::fs_mgr {
inline bool EnsurePathUnmounted(int*,const string& path) {
    LifecycleProbe::effect("fs-mgr-unmount.callback:"+path); return LifecycleProbe::ensure_unmount_succeeds;
}
}
enum class FastbootResult { OKAY,FAIL };
struct FastbootDevice {
    bool WriteFail(const string& message) { LifecycleProbe::failures.push_back(message); return false; }
    bool WriteStatus(FastbootResult result,const string& message) {
        if(result==FastbootResult::FAIL)return WriteFail(message);
        LifecycleProbe::effect("fastboot.status.callback"); LifecycleProbe::status.push_back(message); return true;
    }
    void CloseDevice() { LifecycleProbe::effect("fastboot.close.callback"); }
};
bool ShutDownHandler(FastbootDevice*,const std::vector<string>&);
bool RebootHandler(FastbootDevice*,const std::vector<string>&);
bool RebootBootloaderHandler(FastbootDevice*,const std::vector<string>&);
bool RebootFastbootHandler(FastbootDevice*,const std::vector<string>&);
bool RebootRecoveryHandler(FastbootDevice*,const std::vector<string>&);
int ensure_path_unmounted(const string&);
