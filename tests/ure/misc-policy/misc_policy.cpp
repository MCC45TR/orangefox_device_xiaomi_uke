// SPDX-License-Identifier: Apache-2.0
// Calls full production translation units. No writer or policy implementation.
#include <fixture-external.hpp>
#include <BootControl.h>
#include <libboot_control/libboot_control.h>
#include <private/boot_control_definition.h>
#include <bootloader_message/bootloader_message.h>
#include <twinstall/get_args.h>
#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

void SetMiscBlockDeviceForTest(std::string_view);
extern "C" bool write_reboot_bootloader(void);
extern "C" bool write_bootloader_message(const char*);
namespace android::bootable {
uint32_t BootloaderControlLECRC(const bootloader_control*);
bool LoadBootloaderControl(const std::string&,bootloader_control*);
bool UpdateAndSaveBootloaderControl(const std::string&,bootloader_control*);
}
extern "C" int __real_open(const char*,int,...);
extern "C" int __real_open64(const char*,int,...);
namespace fixture {
std::string misc_path;
std::string slot_suffix="_a";
unsigned writable_opens=0,readonly_opens=0;
bool unexpected_open=false;
unsigned passed=0;
}
using MergeStatus=android::hardware::boot::V1_1::MergeStatus;
using Hal=android::hardware::boot::V1_1::implementation::BootControl;
using Core=android::bootable::BootControl;
static void require(bool ok,const std::string& message) { if(!ok) throw std::runtime_error(message); }
static bool record_open(const char* path,int flags) {
    const bool writable=(flags&O_ACCMODE)!=O_RDONLY || (flags&(O_CREAT|O_TRUNC|O_APPEND));
    if(writable) {
        ++fixture::writable_opens;
        struct stat st{};
        if(!path || fixture::misc_path!=path || ::lstat(path,&st)!=0 || !S_ISREG(st.st_mode) || st.st_nlink!=1 || st.st_uid!=::getuid()) {
            fixture::unexpected_open=true; errno=EPERM; return false;
        }
    } else if(path && fixture::misc_path==path) ++fixture::readonly_opens;
    return true;
}
extern "C" int __wrap_open(const char* path,int flags,...) {
    mode_t mode=0; if(flags&O_CREAT) { va_list args; va_start(args,flags); mode=va_arg(args,int); va_end(args); }
    if(!record_open(path,flags)) return -1;
    return __real_open(path,flags,mode);
}
extern "C" int __wrap_open64(const char* path,int flags,...) {
    mode_t mode=0; if(flags&O_CREAT) { va_list args; va_start(args,flags); mode=va_arg(args,int); va_end(args); }
    if(!record_open(path,flags)) return -1;
    return __real_open64(path,flags,mode);
}
static std::vector<unsigned char> bytes() {
    const int fd=__real_open(fixture::misc_path.c_str(),O_RDONLY|O_NOFOLLOW);
    require(fd>=0,"fixture read open failed"); struct stat st{};
    require(::fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==::getuid() && st.st_size==65536,"fixture must remain a private 64 KiB single-link regular file");
    std::vector<unsigned char> result(static_cast<std::size_t>(st.st_size));
    std::size_t done=0; while(done<result.size()) { const auto n=::read(fd,result.data()+done,result.size()-done); require(n>0,"fixture read failed"); done+=static_cast<std::size_t>(n); }
    ::close(fd); return result;
}
static void store(const std::vector<unsigned char>& data) {
    struct stat before{};
    if(::lstat(fixture::misc_path.c_str(),&before)==0) require(S_ISREG(before.st_mode) && before.st_nlink==1 && before.st_uid==::getuid(),"fixture create refused indirect or nonregular target");
    const int fd=__real_open(fixture::misc_path.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
    require(fd>=0,"fixture preparation failed"); std::size_t done=0;
    while(done<data.size()) { const auto n=::write(fd,data.data()+done,data.size()-done); require(n>0,"fixture preparation write failed"); done+=static_cast<std::size_t>(n); }
    require(::fsync(fd)==0,"fixture preparation fsync failed"); ::close(fd);
}
template<class T> static void place(std::vector<unsigned char>& data,std::size_t offset,const T& value) {
    require(offset+sizeof(T)<=data.size(),"fixture layout overflow"); std::memcpy(data.data()+offset,&value,sizeof(T));
}
static void seed(std::string_view command="",std::string_view recovery="",bool valid_crc=true,bool valid_vab=true) {
    std::vector<unsigned char> data(65536); for(std::size_t i=0;i<data.size();i++) data[i]=static_cast<unsigned char>((i*17+29)%251);
    bootloader_message boot{};
    require(command.size()<sizeof(boot.command) && recovery.size()<sizeof(boot.recovery),"fixture boot text too large");
    std::memcpy(boot.command,command.data(),command.size()); std::memcpy(boot.recovery,recovery.data(),recovery.size());
    std::strcpy(boot.stage,"fixture-stage"); std::strcpy(boot.status,"fixture-status"); boot.reserved[7]='Z';
    place(data,BOOTLOADER_MESSAGE_OFFSET_IN_MISC,boot);
    bootloader_control ctrl{}; std::strcpy(ctrl.slot_suffix,"_a"); ctrl.magic=BOOT_CTRL_MAGIC; ctrl.version=BOOT_CTRL_VERSION; ctrl.nb_slot=2;
    ctrl.slot_info[0].priority=14; ctrl.slot_info[0].tries_remaining=7; ctrl.slot_info[0].successful_boot=1;
    ctrl.slot_info[1].priority=9; ctrl.slot_info[1].tries_remaining=7;
    ctrl.crc32_le=android::bootable::BootloaderControlLECRC(&ctrl); if(!valid_crc) ctrl.crc32_le^=1;
    place(data,offsetof(bootloader_message_ab,slot_suffix),ctrl);
    misc_virtual_ab_message vab{}; vab.version=valid_vab?MISC_VIRTUAL_AB_MESSAGE_VERSION:0; vab.magic=valid_vab?MISC_VIRTUAL_AB_MAGIC_HEADER:0;
    vab.merge_status=static_cast<uint8_t>(MergeStatus::SNAPSHOTTED); vab.source_slot=1;
    place(data,SYSTEM_SPACE_OFFSET_IN_MISC+offsetof(misc_system_space_layout,virtual_ab_message),vab);
    misc_memtag_message memtag{}; memtag.version=MISC_MEMTAG_MESSAGE_VERSION; memtag.magic=MISC_MEMTAG_MAGIC_HEADER; memtag.memtag_mode=MISC_MEMTAG_MODE_MEMTAG;
    place(data,SYSTEM_SPACE_OFFSET_IN_MISC+offsetof(misc_system_space_layout,memtag_message),memtag);
    misc_kcmdline_message kcmdline{}; kcmdline.version=MISC_KCMDLINE_MESSAGE_VERSION; kcmdline.magic=MISC_KCMDLINE_MAGIC_HEADER; kcmdline.kcmdline_flags=MISC_KCMDLINE_BINDER_RUST;
    place(data,SYSTEM_SPACE_OFFSET_IN_MISC+offsetof(misc_system_space_layout,kcmdline_message),kcmdline);
    misc_control_message control{}; control.version=MISC_CONTROL_MESSAGE_VERSION; control.magic=MISC_CONTROL_MAGIC_HEADER; control.misctrl_flags=MISC_CONTROL_16KB_BEFORE;
    place(data,SYSTEM_SPACE_OFFSET_IN_MISC+offsetof(misc_system_space_layout,control_message),control);
    store(data); SetMiscBlockDeviceForTest(fixture::misc_path);
}
static void observed(const std::string& name,const std::function<void()>& operation,unsigned minimum_reads=0) {
    const auto before=bytes(); fixture::writable_opens=0; fixture::readonly_opens=0; fixture::unexpected_open=false;
    operation();
    require(fixture::writable_opens==0,name+" performed a writable open"); require(!fixture::unexpected_open,name+" attempted an unexpected writer target");
    require(fixture::readonly_opens>=minimum_reads,name+" did not exercise the actual read path"); require(bytes()==before,name+" changed whole fixture bytes");
    ++fixture::passed; std::cout<<"PASS "<<name<<'\n';
}
static void denied(const std::string& name,const std::function<bool(std::string*)>& operation,bool expect_message=true) {
    observed(name,[&] { std::string err; errno=0; require(!operation(&err),name+" returned success"); require(errno==EPERM,name+" failed without policy EPERM");
        if(expect_message) require(err=="ure-legacy-write-unavailable",name+" lost the policy refusal code"); });
}
static void bcb_suite() {
    seed(); bootloader_message replacement{}; std::strcpy(replacement.command,"boot-recovery");
    const std::vector<std::string> options={"--show_text","--locale=en"};
    denied("bcb-direct-partition",[&](auto* err){return write_misc_partition(&replacement,sizeof(replacement),fixture::misc_path,0,err);});
    denied("bcb-before-missing-target-open",[&](auto* err){return write_misc_partition(&replacement,sizeof(replacement),fixture::misc_path+".absent",0,err);});
    denied("bcb-null-error-pointer",[&](auto*){return write_misc_partition(&replacement,sizeof(replacement),fixture::misc_path,0,nullptr);},false);
    denied("bcb-struct-write",[&](auto* err){return write_bootloader_message(replacement,err);});
    denied("bcb-struct-write-to",[&](auto* err){return write_bootloader_message_to(replacement,fixture::misc_path,err);});
    denied("bcb-options-write",[&](auto* err){return write_bootloader_message(options,err);});
    denied("bcb-options-write-to",[&](auto* err){return write_bootloader_message_to(options,fixture::misc_path,err);});
    denied("bcb-update",[&](auto* err){return update_bootloader_message(options,err);});
    denied("bcb-clear",[&](auto* err){return clear_bootloader_message(err);});
    denied("bcb-reboot-bootloader",[&](auto* err){return write_reboot_bootloader(err);});
    denied("bcb-C-options-write",[&](auto*){return write_bootloader_message("--show_text");},false);
    denied("bcb-C-reboot-bootloader",[&](auto*){return write_reboot_bootloader();},false);
    denied("wipe-package-write",[&](auto* err){return write_wipe_package("fixture wipe package",err);});
    denied("wipe-empty-write",[&](auto* err){return write_wipe_package("",err);});
    misc_virtual_ab_message vab{}; misc_memtag_message memtag{}; misc_kcmdline_message kcmd{}; misc_control_message control{};
    denied("system-virtual-ab-write",[&](auto* err){return WriteMiscVirtualAbMessage(vab,err);});
    denied("system-memtag-write",[&](auto* err){return WriteMiscMemtagMessage(memtag,err);});
    denied("system-kcmdline-write",[&](auto* err){return WriteMiscKcmdlineMessage(kcmd,err);});
    denied("system-control-write",[&](auto* err){return WriteMiscControlMessage(control,err);});
    observed("bcb-in-memory-construction",[&] { bootloader_message boot{}; std::strcpy(boot.stage,"keep-stage"); std::strcpy(boot.status,"keep-status"); boot.reserved[0]='R';
        require(update_bootloader_message_in_struct(&boot,options),"in-memory BCB construction refused");
        require(std::string(boot.command)=="boot-recovery" && std::string(boot.recovery)=="recovery\n--show_text\n--locale=en\n","in-memory BCB arguments changed");
        require(std::string(boot.stage)=="keep-stage" && std::string(boot.status)=="keep-status" && boot.reserved[0]=='R',"in-memory BCB construction clobbered other fields"); });
    observed("bcb-read-and-default-fstab",[&] { std::string err; bootloader_message boot{}; SetMiscBlockDeviceForTest("");
        require(get_misc_blk_device(&err)==fixture::misc_path,"fstab external lookup failed"); require(read_bootloader_message(&boot,&err),"BCB read failed");
        require(std::string(boot.stage)=="fixture-stage","BCB read data changed"); SetMiscBlockDeviceForTest(fixture::misc_path);
        require(read_bootloader_message_from(&boot,fixture::misc_path,&err),"explicit BCB read failed"); },2);
    observed("wipe-package-read",[&] { std::string data,err; require(read_wipe_package(&data,32,&err) && data.size()==32,"wipe read failed"); },1);
    observed("all-system-message-reads",[&] { std::string err;
        require(ReadMiscVirtualAbMessage(&vab,&err) && vab.magic==MISC_VIRTUAL_AB_MAGIC_HEADER,"VAB read failed");
        require(ReadMiscMemtagMessage(&memtag,&err) && memtag.magic==MISC_MEMTAG_MAGIC_HEADER,"memtag read failed");
        require(ReadMiscKcmdlineMessage(&kcmd,&err) && kcmd.magic==MISC_KCMDLINE_MAGIC_HEADER,"kcmdline read failed");
        require(ReadMiscControlMessage(&control,&err) && control.magic==MISC_CONTROL_MAGIC_HEADER,"control read failed");
        bool empty=true; require(CheckReservedSystemSpaceEmpty(&empty,&err) && !empty,"reserved-byte reader did not report the fixture sentinels"); },5);
}
static void invalid_init_suite(bool invalid_crc) {
    seed("","",!invalid_crc,invalid_crc); const std::string label=invalid_crc?"invalid-CRC":"invalid-VAB";
    Core core; Hal hal;
    for(int attempt=1;attempt<=3;attempt++) {
        observed(label+"-core-Init-attempt-"+std::to_string(attempt),[&]{require(!core.Init(),label+" repeated Init returned success");},1);
        observed(label+"-HAL-Init-attempt-"+std::to_string(attempt),[&]{require(!hal.Init(),label+" HAL repeated Init returned success");},1);
    }
    observed(label+"-HAL-fetch-refuses",[&]{auto* result=android::hardware::boot::V1_1::implementation::HIDL_FETCH_IBootControl("fixture");
        if(result) { delete result; throw std::runtime_error(label+" HAL factory returned an initialized module"); }},1);
}
static void hal_suite() {
    invalid_init_suite(true); invalid_init_suite(false); seed();
    bootloader_control ctrl{}; require(android::bootable::LoadBootloaderControl(fixture::misc_path,&ctrl),"valid control fixture read failed");
    const auto original=ctrl;
    denied("boot-control-direct-save",[&](auto*){return android::bootable::UpdateAndSaveBootloaderControl(fixture::misc_path,&ctrl);},false);
    require(std::memcmp(&ctrl,&original,sizeof(ctrl))==0,"denied boot control save changed caller data");
    Core core; Hal hal;
    observed("valid-CRC-and-VAB-core-init",[&]{require(core.Init(),"valid core Init failed");},2);
    observed("valid-CRC-and-VAB-HAL-init",[&]{require(hal.Init(),"valid HAL Init failed");},2);
    observed("valid-core-read-only-queries",[&] {
        require(core.GetNumberSlots()==2 && core.GetCurrentSlot()==0 && core.GetActiveBootSlot()==0,"valid slot inventory changed");
        require(core.IsSlotBootable(1) && core.IsSlotMarkedSuccessful(0) && !core.IsSlotMarkedSuccessful(1),"valid slot status changed");
        require(core.IsValidSlot(1) && !core.IsValidSlot(2) && std::string(core.GetSuffix(1))=="_b" && core.GetSuffix(2)==nullptr,"valid suffix query changed");
        require(core.GetSnapshotMergeStatus()==MergeStatus::SNAPSHOTTED,"merge status query changed"); },5);
    observed("valid-HAL-read-only-queries",[&] {
        using BoolResult=android::hardware::boot::V1_0::BoolResult;
        require(static_cast<uint32_t>(hal.getNumberSlots())==2 && static_cast<uint32_t>(hal.getCurrentSlot())==0,"HAL slot inventory changed");
        require(static_cast<BoolResult>(hal.isSlotBootable(1))==BoolResult::TRUE && static_cast<BoolResult>(hal.isSlotBootable(2))==BoolResult::INVALID_SLOT,"HAL slot query changed");
        require(static_cast<BoolResult>(hal.isSlotMarkedSuccessful(0))==BoolResult::TRUE,"HAL successful query changed");
        std::string suffix; hal.getSuffix(1,[&](const auto& value){suffix=value;}); require(suffix=="_b","HAL suffix query changed");
        require(static_cast<MergeStatus>(hal.getSnapshotMergeStatus())==MergeStatus::SNAPSHOTTED,"HAL merge status query changed"); },3);
    denied("boot-control-mark-success",[&](auto*){return core.MarkBootSuccessful();},false);
    denied("boot-control-set-active",[&](auto*){return core.SetActiveBootSlot(1);},false);
    denied("boot-control-set-unbootable",[&](auto*){return core.SetSlotAsUnbootable(1);},false);
    denied("boot-control-set-merge-status",[&](auto*){return core.SetSnapshotMergeStatus(MergeStatus::MERGING);},false);
    const auto callback=[&](const auto& result){require(!result.success && result.errMsg=="Operation failed","HAL writer callback reported success");};
    denied("HAL-mark-success",[&](auto*){hal.markBootSuccessful(callback);return false;},false);
    denied("HAL-set-active",[&](auto*){hal.setActiveBootSlot(1,callback);return false;},false);
    denied("HAL-set-unbootable",[&](auto*){hal.setSlotAsUnbootable(1,callback);return false;},false);
    denied("HAL-set-merge-status",[&](auto*){return static_cast<bool>(hal.setSnapshotMergeStatus(MergeStatus::MERGING));},false);
    observed("valid-HAL-factory-read-only",[&]{std::unique_ptr<android::hardware::boot::V1_1::IBootControl> object(android::hardware::boot::V1_1::implementation::HIDL_FETCH_IBootControl("fixture"));
        require(object!=nullptr && static_cast<uint32_t>(object->getNumberSlots())==2,"HAL factory failed valid read-only initialization");},2);
}
static void args_case(const std::string& name,const std::string& command,const std::string& recovery,std::vector<std::string> cli,const std::vector<std::string>& expected) {
    seed(command,recovery); std::vector<char*> pointers; for(auto& item:cli) pointers.push_back(item.data()); int argc=static_cast<int>(pointers.size()); char** argv=pointers.data();
    observed(name,[&] { const auto selected=args::get_args(&argc,&argv); require(selected==expected,name+" changed chosen recovery arguments");
        require(stage=="fixture-stage",name+" changed the BCB stage"); require(argc==static_cast<int>(cli.size()),name+" changed the caller argc"); },2);
    denied(name+"-clear-refuses",[&](auto* err){return clear_bootloader_message(err);});
}
static void args_suite() {
    args_case("get-args-empty","","",{"recovery"},{"recovery"});
    args_case("get-args-boot-recovery","boot-recovery","recovery\n--show_text\n--locale=en\n",{"recovery"},{"recovery","--show_text","--locale=en"});
    args_case("get-args-boot-fastboot","boot-fastboot","",{"recovery"},{"recovery","--fastboot"});
    args_case("get-args-boot-rescue","boot-rescue","",{"recovery"},{"recovery","--rescue"});
    args_case("get-args-CLI-overrides-recovery","boot-recovery","recovery\n--locale=old\n",{"recovery","--show_text","--locale=en"},{"recovery","--show_text","--locale=en"});
    args_case("get-args-CLI-overrides-fastboot","boot-fastboot","",{"recovery","--show_text"},{"recovery","--show_text"});
    args_case("get-args-CLI-overrides-rescue","boot-rescue","",{"recovery","--show_text"},{"recovery","--show_text"});
}
int main(int argc,char** argv) {
    try {
        require(argc==3,"private fixture directory and suite required"); struct stat directory{};
        require(::lstat(argv[1],&directory)==0 && S_ISDIR(directory.st_mode) && directory.st_uid==::getuid() && (directory.st_mode&0777)==0700,"fixture directory must be owned, private and direct");
        fixture::misc_path=std::string(argv[1])+"/misc.bin"; const std::string suite=argv[2];
        if(suite=="all" || suite=="bcb") bcb_suite();
        if(suite=="all" || suite=="hal") hal_suite();
        if(suite=="all" || suite=="get-args") args_suite();
        if(suite=="invalid-crc") invalid_init_suite(true);
        if(suite=="invalid-vab") invalid_init_suite(false);
        require(fixture::passed>0,"unknown or empty suite"); std::cout<<"RESULT cases="<<fixture::passed<<" writable_opens=0 whole_fixture_unchanged=true physical_device=false\n";
    } catch(const std::exception& error) { std::cerr<<"FAIL "<<error.what()<<'\n'; return 1; }
}
