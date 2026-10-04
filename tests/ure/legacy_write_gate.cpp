// SPDX-License-Identifier: Apache-2.0
// No real block devices, mounts, HAL calls or command execution in this fixture.
#include "write-gate-hooks.h"
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>

void gui_err(const char* text) { GateProbe::errors.emplace_back(text); }
namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
string contents(const string& path) { std::ifstream file(path,std::ios::binary); return {std::istreambuf_iterator<char>(file),{}}; }
void unchanged(const string& original) {
    require(GateProbe::side_effects==0,"Native entry point reached a mutating callback");
    require(contents(GateProbe::fixture)==original,"Disposable storage bytes changed");
}
void format_and_slot(const string& original) {
    TWPartitionManager manager;
    for (bool virtual_ab : {false,true}) {
        GateProbe::reset(); GateProbe::virtual_ab=virtual_ab;
        require(manager.Format_Data()==0,"Format Data reported success");
        require(GateProbe::lookups==0 && GateProbe::probes==0,"Format Data performed pre-denial lookup/property/mount work");
        require(GateProbe::namespace_effects==0,"Format Data unmounted metadata before refusal");
        require(GateProbe::errors.size()==1,"Format Data did not display one actionable refusal");
        unchanged(original);
    }
    GateProbe::reset();
    manager.Set_Active_Slot("A");
    require(manager.Active_Slot_Display=="A","Initial observed slot was lost");
    require(GateProbe::hal==0 && GateProbe::data_manager==0,"Initial slot observation touched the HAL");
    manager.Set_Active_Slot("A");
    require(GateProbe::errors.empty(),"No-op slot observation was refused");
    manager.Set_Active_Slot("B");
    require(manager.Active_Slot_Display=="A" && GateProbe::hal==0 && GateProbe::data_manager==0,"Denied slot change had a side effect");
    require(GateProbe::errors.size()==1,"Slot switch did not expose refusal");
    manager.Set_Active_Slot("invalid");
    require(manager.Active_Slot_Display=="A","Invalid slot changed state");
    unchanged(original);
}
void mounts(const string& original) {
    GateProbe::reset();
    TWPartition partition;
    partition.Change_Mount_Read_Only(false);
    require(partition.Mount_Read_Only,"Readonly preference was made writable");
    partition.Mount_Read_Only=false;
    partition.Change_Mount_Read_Only(true);
    require(partition.Mount_Read_Only,"Safer readonly transition was blocked");
    GateProbe::reset(); partition.Mount_Read_Only=false;
    require(!partition.Mount(true),"Writable mount was accepted");
    require(GateProbe::probes==0 && GateProbe::mounts.empty() && GateProbe::commands.empty(),"Writable mount looked up or opened storage");
    unchanged(original);
    partition.Mount_Read_Only=true;
    for (const string fs : {"ext4","f2fs","vfat"}) {
        GateProbe::reset(); partition.Current_File_System=partition.Fstab_File_System=fs;
        GateProbe::mount_results={-1,0};
        require(partition.Mount(true),"Readonly mount fallback failed");
        require(GateProbe::mounts.size()==2,"Readonly retry was not exercised");
        for (const auto& mount : GateProbe::mounts) {
            require((mount.flags & MS_RDONLY)!=0,"Readonly retry lost MS_RDONLY");
            const string option=ure::legacy_read_only_recovery_option(fs);
            require(option.empty() || mount.options.find(option)!=string::npos,"Readonly retry allowed journal replay");
        }
        unchanged(original);
    }
    for (bool failure : {false,true}) {
        GateProbe::reset(); GateProbe::mounted=true; GateProbe::mounted_read_only=false; GateProbe::statvfs_failure=failure;
        require(!partition.Mount(false),"Existing writable/unknown mount was treated as safe");
        require(GateProbe::mounts.empty(),"Unsafe existing mount was remounted automatically");
        unchanged(original);
    }
    GateProbe::reset(); GateProbe::mounted=true;
    require(partition.Mount(true),"Existing readonly mount was refused");
    unchanged(original);
    GateProbe::reset(); partition.Current_File_System=partition.Fstab_File_System="ntfs";
    require(partition.Mount(true),"Readonly NTFS mount was refused");
    require(GateProbe::commands.size()==1 && GateProbe::commands[0].find(" -o ro ")!=string::npos,"NTFS helper lost readonly option");
    unchanged(original);
    GateProbe::reset(); partition.Current_File_System=partition.Fstab_File_System="exfat";
#ifdef TW_NO_EXFAT_FUSE
    GateProbe::mount_results={-1,-1,0};
#endif
    require(partition.Mount(true),"Readonly exFAT mount was refused");
    require(GateProbe::commands.size()==1 && GateProbe::commands[0].find(" -o ro,")!=string::npos,"exFAT helper did not mount readonly");
#ifdef TW_NO_EXFAT_FUSE
    // The detection mount is explicitly unmounted before the kernel retry.
    require(GateProbe::namespace_effects==1,"Unexpected exFAT detection cleanup");
    require(GateProbe::mounts.size()==3 && GateProbe::mounts.back().filesystem=="vfat","Kernel exFAT to FAT fallback was not exercised");
#endif
    unchanged(original);
}
void fastboot(const string& original) {
    PartitionHandle handle;
    for (int flags : {O_WRONLY,O_RDWR,O_RDONLY|O_TRUNC,O_RDONLY|O_CREAT,O_RDONLY|O_APPEND}) {
        GateProbe::reset();
        require(!OpenPartition(nullptr,"logical-fixture",&handle,flags),"Unsafe block open flags were accepted");
        require(GateProbe::lookups==0 && handle.opens==0,"Unsafe open created a mapper or opened a partition");
        unchanged(original);
    }
    for (const string name : {"logical-fixture","physical-fixture"}) {
        GateProbe::reset();
        require(OpenPartition(nullptr,name,&handle,O_RDONLY|O_CLOEXEC),"Readonly block open was rejected");
        require(GateProbe::lookups==2,"Readonly block lookup took an unexpected path");
        unchanged(original);
    }
    const std::vector<string> denied={"flash","erase","set_active","create-logical-partition","delete-logical-partition",
        "resize-logical-partition","update-super","snapshot-update","gsi","oem","unknown-vendor-command"};
    FastbootDevice device;
    for (const auto& cmd : denied) device.kCommandMap[cmd]=[cmd](auto*,const auto&) {
        GateProbe::dispatched.push_back(cmd); GateProbe::write_effect(); return true;
    };
    for (const auto& cmd : denied) device.transport_->input.push_back(cmd=="oem" ? "oem format" : cmd+":userdata");
    GateProbe::reset(); device.ExecuteCommands();
    require(GateProbe::dispatched.empty(),"Fastboot dispatched a mutating or unknown command");
    require(device.failures.size()==denied.size(),"Fastboot did not return FAIL for each refused command");
    for (const auto& error : device.failures) require(error.starts_with("ure-legacy-write-unavailable:"),"Fastboot refusal had no stable code");
    unchanged(original);
    const std::vector<string> allowed={"getvar","download","fetch","reboot","reboot-bootloader","reboot-fastboot","reboot-recovery","shutdown"};
    for (const auto& cmd : allowed) {
        require(ure::legacy_fastboot_command_permitted(cmd),"Reviewed protocol command was rejected");
        device.kCommandMap[cmd]=[cmd](auto*,const auto&) { GateProbe::dispatched.push_back(cmd); return true; };
        device.transport_->input.push_back(cmd+":fixture");
    }
    GateProbe::reset(); device.failures.clear(); device.ExecuteCommands();
    require(GateProbe::dispatched==allowed && device.failures.empty(),"Read/transport/reboot dispatch changed");
    unchanged(original);
    device.transport_->input={"flash:\nuserdata",string("getvar:\0all",11),string(64,'x'),string(1,static_cast<char>(0xff))};
    GateProbe::reset(); device.ExecuteCommands();
    require(GateProbe::dispatched.empty(),"Malformed/control-byte command was dispatched");
    require(device.failures.size()==4,"Malformed command lacked failure status");
    unchanged(original);
    for (int invalid : {-2,0,65}) {
        device.transport_->invalid_result=invalid;
        GateProbe::reset(); device.ExecuteCommands();
        require(GateProbe::dispatched.empty(),"Invalid transport length was dispatched");
        unchanged(original);
    }
    device.transport_->invalid_result=-1; device.output_ok=false;
    device.transport_->input={"flash:userdata","getvar:all"};
    GateProbe::reset(); device.ExecuteCommands();
    require(GateProbe::dispatched.empty() && device.transport_->input.size()==1,"Failed refusal response continued command execution");
    unchanged(original);
}
}
int main() {
    char directory[]="/tmp/ure-write-gate-XXXXXX";
    if (!mkdtemp(directory)) return 1;
    const std::filesystem::path root(directory);
    try {
        GateProbe::fixture=(root/"userdata.fixture").string();
        const string original(8192,'#');
        { std::ofstream file(GateProbe::fixture,std::ios::binary); file << original; }
        require(!ure::live_storage_backend_accepted(),"Unaccepted live backend was enabled");
        for (int operation=-1; operation<=static_cast<int>(ure::LegacyWrite::EncryptionAccess)+1; ++operation) {
            const auto decision=ure::legacy_write_decision(static_cast<ure::LegacyWrite>(operation));
            require(!decision.allowed && string(decision.code)=="ure-legacy-write-unavailable","Unknown/managed write was authorized");
        }
        for (const string command : {"FLASH","flash "," getvar","","continue","boot","oem reboot"})
            require(!ure::legacy_fastboot_command_permitted(command),"Ambiguous/unknown fastboot command was accepted");
        format_and_slot(original); mounts(original); fastboot(original);
        std::filesystem::remove_all(root);
        std::cout << "Actual Format Data, slot, readonly mounts and fastboot dispatch gates passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; std::filesystem::remove_all(root); return 1;
    }
}
