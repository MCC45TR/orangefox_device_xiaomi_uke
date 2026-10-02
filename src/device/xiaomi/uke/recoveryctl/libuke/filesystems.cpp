// SPDX-License-Identifier: Apache-2.0
// Mutating tools operate on a private staged image. The existing chunk journal
// owns the only original-target writes, readback, interruption recovery and rollback.
#include "uke.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <linux/fs.h>
#include <set>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace ure {
namespace {
std::string seal(Value value) { value.removeMember("plan_sha256"); return sha256(json(value)); }
std::string formatter(const std::string& type) {
    if(type=="ext4")return "mke2fs";
    if(type=="vfat")return "mkfs.fat";
    if(type=="exfat")return "mkfs.exfat";
    if(type=="ntfs")return "mkfs.ntfs";
    if(type=="btrfs")return "mkfs.btrfs";
    if(type=="f2fs")return tool_available("make_f2fs") ? "make_f2fs" : "mkfs.f2fs";
    throw Error("unsupported-filesystem","No reviewed formatter for this filesystem");
}
std::string resizer(const std::string& type) {
    if(type=="ext4")return "resize2fs";
    if(type=="f2fs")return "resize.f2fs";
    if(type=="ntfs")return "ntfsresize";
    if(type=="vfat")return "fatresize";
    throw Error("resize-unavailable","This filesystem requires a reviewed copy-and-recreate workflow; no in-place offline resizer is available");
}
std::string checker(const std::string& type) {
    if(type=="ext4")return "e2fsck";
    if(type=="vfat")return "fsck.fat";
    if(type=="exfat")return "fsck.exfat";
    if(type=="ntfs") {
#ifdef __ANDROID__
        return "fsck.ntfs";
#else
        return "ntfsfix";
#endif
    }
    if(type=="f2fs")return "fsck.f2fs";
    if(type=="btrfs")return "btrfs";
    throw Error("unsupported-filesystem","No reviewed checker for this filesystem");
}
Value process(const ProcessResult& result) { Value out; out["exit_status"]=result.status; out["timed_out"]=result.timed_out; out["output"]=redact(result.output); return out; }
void successful(const ProcessResult& result,const std::string& tool,bool repair=false) {
    require(!result.timed_out && (result.status==0 || (repair && (tool=="e2fsck" || tool=="fsck.fat") && result.status==1)),
        "filesystem-tool-failed","The staged filesystem tool failed; the original target was not written");
}
Value record(const Root& store,const std::string& name) {
    const auto st=store.stat(name); require(S_ISREG(st.st_mode) && st.st_uid==::geteuid() && st.st_nlink==1 && (st.st_mode&07777)==0600,
        "unsafe-filesystem-journal","Filesystem records must be private single-link regular files"); return parse_json(store.read(name,4*1024*1024));
}
Fd lock(const Root& store) {
    auto fd=store.open("operation.lock",O_RDWR|O_CREAT,0600); struct stat st{};
    require(::fstat(fd.get(),&st)==0 && S_ISREG(st.st_mode) && st.st_uid==::geteuid() && st.st_nlink==1 && (st.st_mode&07777)==0600 && ::flock(fd.get(),LOCK_EX|LOCK_NB)==0,
        "operation-busy","Filesystem journal is unsafe or already in use"); return fd;
}
void check_plan(const Value& plan) {
    require(plan["schema"]==1 && plan["operation"]=="filesystem.manage" && plan["request"].isObject() && identifier(plan["operation_id"].asString()) &&
        identifier(plan["firmware_profile"].asString()) && hash_valid(plan["source_sha256"].asString()) && hash_valid(plan["plan_sha256"].asString()) && seal(plan)==plan["plan_sha256"].asString(),
        "invalid-filesystem-plan","Invalid sealed filesystem plan");
}
void copy(int source,int destination,std::uint64_t bytes) {
    require(bytes<=INT64_MAX && ::ftruncate(destination,static_cast<off_t>(bytes))==0,"io-error","Cannot size the private staged filesystem");
    if(::ioctl(destination,FICLONE,source)==0) { require(::fsync(destination)==0,"io-error","Cannot sync reflinked filesystem"); return; }
    std::array<char,1024*1024> buffer{}; std::uint64_t offset=0;
    while(offset<bytes) {
        const auto count=::pread(source,buffer.data(),static_cast<std::size_t>(std::min<std::uint64_t>(bytes-offset,buffer.size())),static_cast<off_t>(offset));
        if(count<0 && errno==EINTR)continue;
        require(count>0,"truncated-source","Filesystem source ended early"); std::size_t done=0;
        while(done<static_cast<std::size_t>(count)) { const auto written=::pwrite(destination,buffer.data()+done,static_cast<std::size_t>(count)-done,static_cast<off_t>(offset+done));
            if(written<0 && errno==EINTR)continue;
            require(written>0,"io-error","Cannot stage filesystem bytes"); done+=static_cast<std::size_t>(written); }
        offset+=static_cast<std::uint64_t>(count);
    }
    require(::fsync(destination)==0,"io-error","Cannot sync staged filesystem");
}
void filesystem_target(const StorageTarget& target) {
    require(target.identity["kind"]=="regular-image" || target.identity["partition"]==true,"whole-disk-rejected","Filesystem tools cannot operate on a whole UFS disk");
    const auto table=gpt_inspect(target.descriptor.get(),target.identity["logical_sector_bytes"].asUInt());
    require(!table["primary"]["valid"].asBool() && !table["backup"]["valid"].asBool() && !table["protective_mbr_valid"].asBool(),
        "whole-disk-rejected","A GPT or protective MBR is not a filesystem target");
    for(const std::uint64_t sector:{512ULL,4096ULL}) {
        std::array<char,8> header{}; const auto bytes=target.identity["bytes"].asUInt64();
        for(const auto offset:{sector,bytes>sector ? bytes-sector : sector})
            require(::pread(target.descriptor.get(),header.data(),header.size(),static_cast<off_t>(offset))!=static_cast<ssize_t>(header.size()) ||
                std::string_view(header.data(),header.size())!="EFI PART","whole-disk-rejected","A damaged or differently aligned GPT is not a filesystem target");
    }
    const auto signature=filesystem_probe(target.descriptor.get());
    require(signature["encryption"]=="none" && signature["type"]!="wim" && signature["type"]!="erofs", "protected-filesystem", "Encrypted containers, WIM and Android read-only images require their own workflow");
    if(target.identity["kind"]=="live-block")require(target.identity["label"]=="uke_linux" || target.identity["label"]=="uke_esp" || target.identity["label"]=="uke_windows" || target.identity["label"]=="uke_home",
        "protected-partition","Android firmware, userdata, metadata and unclassified partitions are protected");
}
// fatresize operates through a partition table, and its superfloppy path can
// abort in libparted. Give it a disposable single-partition envelope; only the
// validated filesystem payload is copied back into our private working file.
ProcessResult resize_fat(const Root& store,int working,std::uint64_t capacity,std::uint64_t wanted) {
    constexpr std::uint64_t start=1024*1024;
    require(capacity/512<=UINT32_MAX-2048,"unsupported-fat-size","FAT resize envelope exceeds MBR sector bounds");
    const auto boot=storage_read(working,0,512);
    auto integer=[&](std::size_t at,unsigned bytes) { std::uint64_t value=0;
        for(unsigned i=0;i<bytes;++i)value|=static_cast<std::uint64_t>(static_cast<unsigned char>(boot[at+i]))<<(8*i);
        return value;
    };
    require(integer(11,2)==512,"unsupported-fat-sector","The reviewed FAT resize envelope requires 512-byte filesystem sectors");
    const auto backup_sector=integer(50,2),reserved=integer(14,2);
    const bool fat32=integer(22,2)==0;
    require(!fat32 || (backup_sector>0 && backup_sector<reserved && backup_sector*512+512<=capacity),
        "invalid-fat-backup-sector","FAT32 backup boot sector is outside the reserved area");
    const auto hidden=boot.substr(28,4);
    auto disk=store.open("fat-resize-envelope.img",O_RDWR|O_CREAT|O_EXCL,0600);
    require(::ftruncate(disk.get(),static_cast<off_t>(capacity+start))==0,"io-error","Cannot size the private FAT resize envelope");
    std::array<char,512> mbr{}; mbr[446+4]=fat32 ? 0x0c : 0x0e;
    auto put=[&](std::size_t at,std::uint64_t value) { for(unsigned i=0;i<4;++i)mbr[at+i]=static_cast<char>((value>>(8*i))&255); };
    put(446+8,start/512); put(446+12,capacity/512); mbr[510]=0x55; mbr[511]=static_cast<char>(0xaa);
    require(::pwrite(disk.get(),mbr.data(),mbr.size(),0)==static_cast<ssize_t>(mbr.size()),"io-error","Cannot write the private FAT envelope header");
    auto transfer=[&](int source,int destination,std::uint64_t input,std::uint64_t output) {
        std::array<char,1024*1024> buffer{};
        for(std::uint64_t at=0;at<capacity;) {
            const auto amount=static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(),capacity-at)); std::size_t done=0;
            while(done<amount) { const auto count=::pread(source,buffer.data()+done,amount-done,static_cast<off_t>(input+at+done));
                if(count<0 && errno==EINTR)continue;
                require(count>0,"io-error","Cannot read the private FAT envelope payload"); done+=static_cast<std::size_t>(count); }
            done=0; while(done<amount) { const auto count=::pwrite(destination,buffer.data()+done,amount-done,static_cast<off_t>(output+at+done));
                if(count<0 && errno==EINTR)continue;
                require(count>0,"io-error","Cannot copy the private FAT envelope payload"); done+=static_cast<std::size_t>(count); }
            at+=amount;
        }
    };
    transfer(working,disk.get(),0,start); require(::fsync(disk.get())==0,"io-error","Cannot sync FAT resize envelope");
    // The reviewed fatresize adapter computes an inclusive end as start +
    // requested_sectors. Reserve one sector and enforce the final upper bound.
    auto result=run_tool("fatresize",{"-f","-n","1","-s",std::to_string(wanted-512),"/proc/self/fd/"+std::to_string(disk.get())},300,{}, {disk.get()});
    if(result.status!=0 || result.timed_out)return result;
    const auto resized_boot=storage_read(disk.get(),start,512);
    require((resized_boot[22]==0 && resized_boot[23]==0)==fat32,"fat-variant-changed","FAT resize cannot silently convert the original FAT16/FAT32 variant");
    const auto table=storage_read(disk.get(),0,512);
    require(table.substr(446+8,4)==std::string(mbr.data()+446+8,4),"fat-envelope-moved","FAT resizer moved its private partition start");
    std::uint64_t sectors=0; for(unsigned i=0;i<4;++i)sectors|=static_cast<std::uint64_t>(static_cast<unsigned char>(table[446+12+i]))<<(8*i);
    require(sectors*512<=wanted && sectors*512>=32*1024*1024,"fat-envelope-size","FAT resizer produced an unexpected private geometry");
    transfer(disk.get(),working,start,0);
    // Hidden-sector addresses belong to the original partition, not the envelope.
    require(::pwrite(working,hidden.data(),hidden.size(),28)==4,"io-error","Cannot preserve FAT hidden-sector metadata");
    if(fat32)require(::pwrite(working,hidden.data(),hidden.size(),static_cast<off_t>(backup_sector*512+28))==4,"io-error","Cannot preserve backup FAT hidden-sector metadata");
    const auto label_at=fat32 ? 71 : 43; const auto label=boot.substr(static_cast<std::size_t>(label_at),11);
    require(::pwrite(working,label.data(),label.size(),label_at)==11,"io-error","Cannot preserve FAT volume label metadata");
    if(fat32)require(::pwrite(working,label.data(),label.size(),static_cast<off_t>(backup_sector*512+static_cast<std::uint64_t>(label_at)))==11,
        "io-error","Cannot preserve backup FAT volume label metadata");
    return result;
}
void prepare_image(const Root& system,StorageTarget& target,const Root& store,const Value& plan,Value& progress) {
    auto write=store.open("working.img",O_RDWR|O_CREAT|O_EXCL,0600); copy(target.descriptor.get(),write.get(),target.identity["bytes"].asUInt64());
    require(sha256(write.get())==plan["source_sha256"].asString(),"stale-source","Original filesystem changed during staging");
    const auto& request=plan["request"]; const auto action=request["action"].asString(),type=request["filesystem"].asString(); const auto fd="/proc/self/fd/"+std::to_string(write.get());
    std::vector<std::string> args; const auto tool=plan["tool"].asString();
    if(action=="format") {
        const auto label=request.get("label","URE").asString();
        if(type=="ext4")args={"-q","-F","-t","ext4","-L",label,fd};
        else if(type=="vfat")args={"-F","32","-n",label,fd};
        else if(type=="exfat")args={"-L",label,fd};
        else if(type=="ntfs")args={"-F","-Q","-L",label,fd};
        else if(type=="f2fs")args={"-f","-l",label,fd};
        else if(type=="btrfs")args={"-f","-L",label,fd};
    } else if(action=="repair") {
        if(type=="ext4")args={"-f","-p",fd};
        else if(type=="vfat" || type=="exfat")args={"-a",fd};
        else if(type=="f2fs")args={"-f","-y",fd};
        else if(type=="ntfs")args={fd};
    } else {
        const auto bytes=request["target_bytes"].asUInt64();
        if(type=="ext4") { const auto checked=run_tool("e2fsck",{"-f","-p",fd},300,{}, {write.get()});
            progress["precheck_tool"]="e2fsck"; progress["precheck_tool_result"]=process(checked); store.save_record("state.json",progress,true);
            successful(checked,"e2fsck",true); args={fd,std::to_string(bytes/1024)+"K"}; }
        else if(type=="ntfs") { const auto checked=run_tool("ntfsresize",{"--no-action","--size",std::to_string(bytes),fd},300,{}, {write.get()}); successful(checked,"ntfsresize"); args={"--force","--size",std::to_string(bytes),fd}; }
        else if(type=="f2fs")args={"-s","-t",std::to_string(bytes/512),fd};
        else if(type=="vfat")args={"-s",std::to_string(bytes),fd};
    }
    require(!args.empty(),"unsupported-filesystem-operation","No reviewed staged tool arguments");
    progress["state"]="TOOL_RUNNING"; store.save_record("state.json",progress,true);
    const auto result=action=="resize" && type=="vfat" ? resize_fat(store,write.get(),target.identity["bytes"].asUInt64(),request["target_bytes"].asUInt64()) : run_tool(tool,args,300,{}, {write.get()});
    progress["tool_result"]=process(result); store.save_record("state.json",progress,true); successful(result,tool,action=="repair");
    // Some offline resizers truncate regular files. A staged file represents a
    // fixed-capacity partition: restore its tail without changing original GPT.
    struct stat prepared{}; const auto capacity=target.identity["bytes"].asUInt64();
    require(::fstat(write.get(),&prepared)==0 && prepared.st_size>=0 && static_cast<std::uint64_t>(prepared.st_size)<=capacity,
        "filesystem-tool-geometry-changed","A staged tool exceeded the reviewed container capacity");
    if(static_cast<std::uint64_t>(prepared.st_size)<capacity)
        require(action=="resize" && ::ftruncate(write.get(),static_cast<off_t>(capacity))==0,"filesystem-tool-geometry-changed","Cannot preserve the reviewed container capacity");
    if(action=="resize" && type=="ntfs") {
        // ntfsresize cannot know a future partition boundary. Here the
        // container is deliberately unchanged: its alternate boot sector must
        // match the new primary at the retained physical end (ntfsfix policy).
        const auto header=storage_read(write.get(),0,512);
        auto number=[&](std::size_t at,unsigned size) { std::uint64_t value=0;
            for(unsigned i=0;i<size;++i)value|=static_cast<std::uint64_t>(static_cast<unsigned char>(header[at+i]))<<(8*i);
            return value; };
        const auto sector=number(11,2),sectors=number(40,8);
        require(header.substr(3,8)=="NTFS    " && sector>=512 && sector<=4096 && (sector&(sector-1))==0 && capacity%sector==0 &&
            sectors>0 && sectors<capacity/sector && sectors<=request["target_bytes"].asUInt64()/sector,
            "invalid-ntfs-resize-geometry","NTFS staged boot geometry does not fit the requested size and retained container");
        const auto primary=storage_read(write.get(),0,static_cast<std::size_t>(sector));
        require(::pwrite(write.get(),primary.data(),primary.size(),static_cast<off_t>(capacity-sector))==static_cast<ssize_t>(primary.size()),
            "io-error","Cannot sync staged NTFS alternate boot sector with retained capacity");
    }
    require(::fsync(write.get())==0,"io-error","Cannot sync transformed filesystem");
    const auto signature=filesystem_probe(write.get()); require(signature["type"]==type,"filesystem-mismatch","Transformed filesystem signature differs from the reviewed request");
    if(action=="resize" && plan["signature"]["uuid"].isString())require(signature["uuid"]==plan["signature"]["uuid"],"filesystem-uuid-changed","Resize cannot change the original filesystem identity");
    // Pass a genuinely read-only descriptor to the independent post-checker.
    auto read=store.open("working.img",O_RDONLY); const auto checked=filesystem_check(read.get());
    progress["post_check"]=checked; store.save_record("state.json",progress,true);
    require(checked["successful"]==true,"filesystem-post-check-failed","Transformed filesystem did not pass its read-only post-check");
    progress["post_check"]=checked; progress["prepared_sha256"]=sha256(read.get()); progress["state"]="PREPARED"; store.save_record("state.json",progress,true);
    storage_revalidate(target,&system); require(sha256(target.descriptor.get())==plan["source_sha256"].asString(),"stale-source","Original target changed while its replacement was prepared");
}
Value transformed(const Root& system,StorageTarget& target,const Root& store,const fs::path& path,const Value& plan,Value& progress) {
    prepare_image(system,target,store,plan,progress);
    filesystem_replacement_backup(system,target,store,"working.img",path/"prepared-backup",plan["firmware_profile"].asString());
    auto application=restore_plan(system,target,path/"prepared-backup",plan["firmware_profile"].asString()); store.save_record("application-plan.json",application);
    progress["state"]="READY_TO_APPLY"; progress["application_plan_sha256"]=application["plan_sha256"]; store.save_record("state.json",progress,true);
    return application;
}
} // namespace
Value filesystem_capabilities() {
    Value out; out["schema"]=1; out["filesystems"]=Value(Json::arrayValue);
    for(const auto* type:{"ext4","f2fs","vfat","exfat","ntfs","btrfs"}) {
        Value item; item["filesystem"]=type; item["format_available"]=tool_available(formatter(type)); item["check_available"]=tool_available(checker(type));
        item["repair_available"]=std::string(type)!="btrfs" && tool_available(checker(type));
        item["offline_resize_available"]=false; try { item["offline_resize_available"]=tool_available(resizer(type)); } catch(const Error& e) { if(e.code!="resize-unavailable")throw; }
        item["resize_geometry"]=std::string(type)=="btrfs" ? "mounted-filesystem ioctl workflow" : std::string(type)=="exfat" ? "copy-and-recreate required; not implemented" : "offline filesystem size only; partition boundary must be planned separately";
        if(std::string(type)=="ntfs")item["repair_scope"]="ntfsfix repairs limited metadata and schedules Windows chkdsk; it is not a complete NTFS repair";
        out["filesystems"].append(item);
    }
    out["physical_test_record"]=false; out["python_required"]=false; out["live_write_backend_ready"]=false; return out;
}
Value filesystem_operation_plan(const Root& system,const StorageTarget& target,const Value& request,const std::string& profile) {
    require(identifier(profile),"invalid-profile","An explicit firmware profile is required"); filesystem_target(target); storage_revalidate(target,&system);
    require(request.isObject() && request["schema"]==1 && request["action"].isString() && request["filesystem"].isString(),"invalid-filesystem-request","Select filesystem action and type");
    for(const auto& key:request.getMemberNames())require(key=="schema" || key=="action" || key=="filesystem" || key=="label" || key=="target_bytes" || key=="erase_confirmed","invalid-filesystem-request","Unknown filesystem request field");
    const auto action=request["action"].asString(),type=request["filesystem"].asString(); const auto bytes=target.identity["bytes"].asUInt64();
    require(bytes>=32*1024*1024 && bytes<=512ULL*1024*1024*1024,"unsupported-filesystem-size","Staged filesystem jobs require 32 MiB to 512 GiB containers");
    std::string tool;
    if(action=="format") { require(request["erase_confirmed"]==true,"erase-confirmation-required","Formatting requires an explicit data-loss choice in the reviewed request"); tool=formatter(type); }
    else { require(action=="repair" || action=="resize","unsupported-filesystem-operation","Select format, repair or resize");
        require(filesystem_probe(target.descriptor.get())["type"]==type,"filesystem-mismatch","Existing filesystem differs from the requested adapter");
        require(!request.isMember("erase_confirmed") && !request.isMember("label"),"invalid-filesystem-request","Repair and resize do not accept format choices");
        require(type!="btrfs","btrfs-mounted-workflow-required","Btrfs mutations require the mounted native manager; unsafe btrfs check --repair is not used");
        tool=action=="repair" ? checker(type) : resizer(type);
    }
    if(action=="resize")require(request["target_bytes"].isUInt64() && request["target_bytes"].asUInt64()>=32*1024*1024 && request["target_bytes"].asUInt64()<=bytes && request["target_bytes"].asUInt64()%4096==0,
        "invalid-resize-size","Filesystem size must be a 4 KiB multiple between 32 MiB and the selected container capacity");
    else require(!request.isMember("target_bytes"),"invalid-filesystem-request","Target size applies only to resize");
    if(action=="resize" && type=="vfat") {
        const auto boot=storage_read(target.descriptor.get(),0,512);
        auto number=[&](std::size_t at,unsigned count) { std::uint64_t value=0; for(unsigned i=0;i<count;++i)value|=static_cast<std::uint64_t>(static_cast<unsigned char>(boot[at+i]))<<(8*i); return value; };
        require(number(11,2)==512,"unsupported-fat-sector","The reviewed FAT resize envelope requires 512-byte filesystem sectors");
        if(number(22,2)==0) {
            const auto clusters=number(13,1); require(clusters>0 && clusters<=128 && (clusters&(clusters-1))==0,"invalid-fat-cluster","Invalid FAT32 cluster size");
            const auto minimum=(65525*clusters+number(14,2)+number(16,1)*number(36,4))*512+4096;
            require(request["target_bytes"].asUInt64()>=minimum,"fat-variant-size-limit","The requested size would risk converting FAT32 to FAT16; preserve the original variant");
        }
    }
    if(request.isMember("label"))require(request["label"].isString() && identifier(request["label"].asString()) && request["label"].asString().size()<=11,"invalid-label","Use an ASCII filesystem label of at most 11 characters");
    require(tool_available(tool) && tool_available(checker(type)),"missing-tool","The selected filesystem adapter or independent checker is not packaged");
    Value plan; plan["schema"]=1; plan["operation"]="filesystem.manage"; plan["operation_id"]=operation_id(); plan["request"]=request; plan["tool"]=tool;
    plan["firmware_profile"]=profile; plan["target_identity"]=target.identity; plan["signature"]=filesystem_probe(target.descriptor.get());
    plan["source_sha256"]=sha256(target.descriptor.get()); storage_revalidate(target,&system); plan["preflight"]=storage_preflight(system,target,profile);
    plan["estimated_max_journal_bytes"]=Json::UInt64(bytes*(action=="resize" && type=="vfat" ? 6 : 5)+64*1024*1024); plan["copy_buffer_bytes"]=1024*1024;
    plan["external_tool_memory_bounded"]=false;
    plan["partition_boundary_changed"]=false; plan["confirmation_required"]=true; plan["private_record"]=true; plan["physical_test_record"]=false;
    plan["risk"]=action=="format" ? "Erase all files in the selected filesystem; preserve a verified complete original image in the application journal before writes" : "Modify only the selected filesystem; keep verified before/after bytes for interruption recovery and rollback";
    plan["plan_sha256"]=seal(plan); return plan;
}
Value filesystem_prepare(const Root& system,StorageTarget& source,const Value& plan,const fs::path& directory,const std::string& confirmation) {
    check_plan(plan); require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the exact preparation plan hash");
    require(source.identity["kind"]=="regular-image" && json(source.identity)==json(plan["target_identity"]),"invalid-stage-source","Preparation selects an unchanged private regular image");
    const auto checked=filesystem_operation_plan(system,source,plan["request"],plan["firmware_profile"].asString());
    require(checked["source_sha256"]==plan["source_sha256"] && checked["tool"]==plan["tool"],"stale-source","Filesystem preparation input or tool differs from review");
    auto store=private_directory(directory,true); auto writer=lock(store); store.save_record("plan.json",plan);
    Value progress; progress["schema"]=1; progress["plan_sha256"]=plan["plan_sha256"]; progress["state"]="VALIDATED";
    progress["source_written"]=false; progress["physical_test_record"]=false; store.save_record("state.json",progress);
    try { prepare_image(system,source,store,plan,progress); return progress; }
    catch(const Error& error) { progress["state"]="FAILED_SAFE"; progress["error_code"]=error.code;
        try { store.save_record("state.json",progress,true); } catch(...) {} throw; }
}
Value filesystem_operation_execute(const Root& system,StorageTarget& target,const Value& plan,const fs::path& path,const std::string& confirmation) {
    check_plan(plan); require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the exact filesystem plan hash");
    require(json(target.identity)==json(plan["target_identity"]),"stale-device","Filesystem selection changed since review");
    filesystem_target(target); storage_revalidate(target,&system); storage_write_gate(target);
    require(sha256(target.descriptor.get())==plan["source_sha256"].asString(),"stale-source","Filesystem content changed since review");
    auto store=private_directory(path,true); auto writer=lock(store); store.save_record("plan.json",plan);
    struct statvfs space{}; require(::fstatvfs(store.fd(),&space)==0 && space.f_frsize && space.f_bavail>=plan["estimated_max_journal_bytes"].asUInt64()/space.f_frsize+1,"insufficient-space","Keep space for the staged image and both complete recovery mirrors");
    Value progress; progress["schema"]=1; progress["plan_sha256"]=plan["plan_sha256"]; progress["state"]="VALIDATED"; store.save_record("state.json",progress);
    try { const auto application=transformed(system,target,store,path,plan,progress);
        const auto result=restore_execute(system,target,application,path/"application",application["plan_sha256"].asString());
        progress["state"]="COMPLETE"; progress["application"]=result; progress["successful"]=true; progress["physical_test_record"]=false;
        store.save_record("state.json",progress,true); return progress;
    } catch(const Error& error) { progress["state"]=store.exists("application") ? "RECOVERY_REQUIRED" : "FAILED_SAFE"; progress["error_code"]=error.code;
        try { store.save_record("state.json",progress,true); } catch(...) {} throw; }
}
Value filesystem_operation_recover(const Root& system,StorageTarget& target,const fs::path& path,const std::string& operation,const std::string& confirmation) {
    auto store=private_directory(path,false); auto writer=lock(store); const auto plan=record(store,"plan.json"); check_plan(plan);
    Value out=record(store,"state.json"); out["plan_sha256"]=plan["plan_sha256"];
    require(operation=="inspect" || operation=="resume" || operation=="rollback" || operation=="cancel","unsupported-filesystem-operation","Unknown filesystem recovery action");
    if(operation!="inspect")require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the exact filesystem journal hash");
    if(store.exists("application")) {
        const auto application=record(store,"application-plan.json");
        if(operation=="inspect")out["application"]=restore_inspect(system,target,path/"application");
        else if(operation=="resume")out["application"]=restore_resume(system,target,path/"application",application["plan_sha256"].asString());
        else if(operation=="rollback")out["application"]=restore_rollback(system,target,path/"application",application["plan_sha256"].asString());
        else out["application"]=restore_cancel(system,target,path/"application",application["plan_sha256"].asString());
        if(operation!="inspect") { out["state"]=operation=="rollback" ? "ROLLED_BACK" : operation=="cancel" ? "CANCELLED_SAFE" : "COMPLETE"; store.save_record("state.json",out,true); }
    } else {
        require(operation=="inspect" || operation=="cancel","prepare-restart-required","Interrupted staging never writes the original target; cancel this store and create a new reviewed plan");
        storage_revalidate(target,&system); require(json(target.identity)==json(plan["target_identity"]) && sha256(target.descriptor.get())==plan["source_sha256"].asString(),"changed-target","Original target changed after staging");
        out["original_unchanged_verified"]=true;
        if(operation=="cancel") { out["state"]="CANCELLED_SAFE"; store.save_record("state.json",out,true); }
    }
    return out;
}
} // namespace ure
