// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <algorithm>
#include <charconv>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace ure {
namespace {
Value image_identity(int fd,const std::string& path,std::uint32_t sector) {
    struct stat st{};
    require(::fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_size>=0,
        "invalid-image","GPT image must be a regular file with one hard link");
    require(sector==512 || sector==4096,"invalid-sector","Select 512 or 4096-byte image sectors");
    Value identity; identity["kind"]="regular-image"; identity["path"]=path; identity["device_family"]="file-image";
    identity["file_device"]=Json::UInt64(st.st_dev); identity["file_inode"]=Json::UInt64(st.st_ino);
    identity["bytes"]=Json::UInt64(static_cast<std::uint64_t>(st.st_size)); identity["logical_sector_bytes"]=sector;
    identity["mtime_seconds"]=Json::Int64(st.st_mtim.tv_sec); identity["mtime_nanoseconds"]=Json::Int64(st.st_mtim.tv_nsec);
    identity["ctime_seconds"]=Json::Int64(st.st_ctim.tv_sec); identity["ctime_nanoseconds"]=Json::Int64(st.st_ctim.tv_nsec);
    identity["mode"]=static_cast<Json::UInt>(st.st_mode & 07777); identity["uid"]=static_cast<Json::UInt>(st.st_uid);
    identity["gid"]=static_cast<Json::UInt>(st.st_gid); identity["private_record"]=true;
    const auto gpt=gpt_inspect(fd,sector); identity["disk_guid"]=gpt["disk_guid"]; return identity;
}
std::uint32_t decimal(const std::string& text) {
    std::uint32_t value=0; const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    require(!text.empty() && parsed.ec==std::errc() && parsed.ptr==text.data()+text.size(),"invalid-sysfs","Invalid kernel device number"); return value;
}
std::string trim(std::string text) {
    while(!text.empty() && (text.back()=='\n' || text.back()=='\r' || text.back()==' '))text.pop_back();
    return text;
}
std::string field(const Root& system,const std::string& path) {
    try { return trim(system.read(path,4096)); } catch(const Error& error) { if(error.code=="path-unavailable")return {}; throw; }
}
Fd block_fd(const Root& system,const Value& object,bool exclusive=false) {
    const auto name=object["kernel_name"].asString(); require(identifier(name),"invalid-device","Invalid kernel block name");
    Fd fd; std::string path;
    for(const auto* prefix:{"dev/block/","dev/"}) {
        try { path=std::string(prefix)+name; fd=system.open(path,O_RDONLY|O_NONBLOCK); break; }
        catch(const Error& error) { if(error.code!="path-unavailable")throw; }
    }
    struct stat st{};
    require(fd.get()>=0 && ::fstat(fd.get(),&st)==0 && S_ISBLK(st.st_mode),"invalid-block","Selected live storage must resolve to a real block device");
    const auto number=object["device_number"].asString(); const auto colon=number.find(':');
    require(colon!=number.npos,"invalid-sysfs","Block device number is missing");
    require(st.st_rdev==makedev(decimal(number.substr(0,colon)),decimal(number.substr(colon+1))),
        "stale-device","Device node does not match the selected sysfs object");
    require(object["bytes"].isUInt64() && storage_bytes(fd.get())==object["bytes"].asUInt64(),"stale-device","Block capacity differs from sysfs inventory");
    if(exclusive) {
        auto claimed=system.open(path,O_RDONLY|O_NONBLOCK|O_EXCL); struct stat current{};
        require(::fstat(claimed.get(),&current)==0 && S_ISBLK(current.st_mode) && current.st_rdev==st.st_rdev &&
            storage_bytes(claimed.get())==object["bytes"].asUInt64(),"stale-device","Block identity changed while acquiring the kernel claim");
        fd=std::move(claimed);
    }
    return fd;
}
Value block_identity(const Root& system,const Value& graph,const Value& object,int fd) {
    const auto bytes=storage_bytes(fd); int sector=0,physical=0,ro=0;
    require(::ioctl(fd,BLKSSZGET,&sector)==0 && (sector==512 || sector==4096),"unsupported-sector","Live logical sector size is unavailable or unsupported");
    require(object["logical_sector_bytes"].isUInt64() && static_cast<std::uint64_t>(sector)==object["logical_sector_bytes"].asUInt64(),
        "stale-device","Logical sector size differs from sysfs inventory");
    require(::ioctl(fd,BLKROGET,&ro)==0,"identity-unavailable","Cannot inspect live block read/write state");
    Value identity; identity["kind"]="live-block";
    for(const auto* key:{"stable_id","kernel_name","sysfs_path","device_number","partition","parent_lun_name","parent_lun_sysfs","partition_index","partuuid","label","owner","write_policy","start_512_sectors","mounts","slaves","holders","mapper_name","mapper_uuid"})identity[key]=object[key];
    identity["bytes"]=Json::UInt64(bytes); identity["logical_sector_bytes"]=sector; identity["read_only_state"]=ro!=0;
    if(::ioctl(fd,BLKPBSZGET,&physical)==0 && physical>0)identity["physical_sector_bytes"]=physical;
    Value parent; for(const auto& item:graph["objects"])if(item["sysfs_path"]==object["parent_lun_sysfs"]) { require(parent.isNull(),"ambiguous-identity","Multiple parent disks match this partition"); parent=item; }
    require(parent.isObject() && parent["partition"]==false,"identity-unavailable","A unique parent disk is required");
    auto disk=block_fd(system,parent); const auto gpt=gpt_inspect(disk.get(),static_cast<std::uint32_t>(sector));
    identity["disk_guid"]=gpt["disk_guid"]; identity["parent_device_number"]=parent["device_number"];
    if(object["partition"]==true) {
        require(gpt["healthy"]==true,"ambiguous-gpt","Partition identity requires matching valid GPT copies");
        Value entry; for(const auto& item:gpt["partitions"])if(json(item["index"])==json(object["partition_index"]))entry=item;
        require(entry.isObject() && object["start_512_sectors"].isUInt64() && entry["start_lba"].asUInt64()*static_cast<std::uint64_t>(sector)==object["start_512_sectors"].asUInt64()*512 &&
            entry["bytes"].asUInt64()==bytes && (object["partuuid"].asString().empty() || object["partuuid"]==entry["partuuid"]) && object["label"]==entry["label"],
            "stale-device","GPT partition range, identity or label differs from kernel inventory");
        identity["partuuid"]=entry["partuuid"]; identity["type_guid"]=entry["type_guid"];
    }
    auto device=parent["sysfs_path"].asString()+"/device";
    try {
        const auto linked=(fs::path(parent["sysfs_path"].asString())/system.link(device)).lexically_normal().generic_string();
        require(linked.starts_with("sys/devices/"),"invalid-sysfs","Disk device link escapes sys/devices"); device=linked;
    } catch(const Error& error) { if(error.code!="path-unavailable")throw; }
    const auto wwid=field(system,device+"/wwid"),serial=field(system,device+"/serial");
    const auto unique=!wwid.empty() ? "wwid:"+wwid : !serial.empty() ? "serial:"+serial : std::string();
    identity["unit_identity_sha256"]=unique.empty() ? Value() : Value(sha256(unique));
    identity["lun_address"]=fs::path(device).filename().string();
    identity["unit_identity_available"]=!unique.empty(); identity["private_record"]=true;
    identity["device_family"]=field(system,"sys/firmware/devicetree/base/model");
    identity["boot_id_sha256"]=sha256(field(system,"proc/sys/kernel/random/boot_id"));
    return identity;
}
}
StorageTarget storage_image(const fs::path& path,std::uint32_t sector,bool writable) {
    const auto full=fs::absolute(path).lexically_normal(); Root parent(full.parent_path());
    auto fd=parent.open(full.filename().string(),O_RDONLY|O_NONBLOCK);
    auto identity=image_identity(fd.get(),full.string(),sector);
    if(writable) {
        auto write_fd=parent.open(full.filename().string(),O_RDWR|O_NONBLOCK);
        require(json(image_identity(write_fd.get(),full.string(),sector))==json(identity),"stale-device","Image changed while selecting write access");
        fd=std::move(write_fd);
    }
    return {std::move(fd),std::move(identity)};
}
StorageTarget storage_select(const Root& system,const std::string& stable_id,bool exclusive_claim) {
    require(stable_id.starts_with("partuuid:") || stable_id.starts_with("sysfs:") || stable_id.starts_with("gpt:"),"invalid-identity","Select a complete Storage Graph or GPT identity");
    const auto graph=storage_graph(system); Value selected;
    for(const auto& object:graph["objects"]) {
        bool matches=object["stable_id"]==stable_id;
        if(stable_id.starts_with("gpt:") && object["partition"]==false) {
            require(uuid(stable_id.substr(4)),"invalid-identity","GPT selection requires a nonzero disk GUID");
            auto fd=block_fd(system,object);
            const auto sector=object["logical_sector_bytes"].asUInt();
            matches=gpt_inspect(fd.get(),sector)["disk_guid"]==stable_id.substr(4);
        }
        if(matches) { require(selected.isNull(),"ambiguous-identity","Multiple live objects match this identity"); selected=object; }
    }
    require(selected.isObject(),"identity-unavailable","Selected live storage was not found uniquely");
    auto fd=block_fd(system,selected,exclusive_claim); auto identity=block_identity(system,graph,selected,fd.get());
    return {std::move(fd),std::move(identity),exclusive_claim};
}
void storage_revalidate(const StorageTarget& target,const Root* system) {
    if(target.identity["kind"]=="regular-image") {
        const auto current=storage_image(target.identity["path"].asString(),target.identity["logical_sector_bytes"].asUInt());
        require(json(current.identity)==json(target.identity) && json(image_identity(target.descriptor.get(),target.identity["path"].asString(),target.identity["logical_sector_bytes"].asUInt()))==json(target.identity),
            "stale-device","Image path, descriptor or contents metadata changed");
    } else {
        require(system && target.identity["kind"]=="live-block","identity-unavailable","Live revalidation requires the selected system root");
        const auto current=storage_select(*system,target.identity["stable_id"].asString());
        require(json(current.identity)==json(target.identity),"stale-device","Live object identity, mounts or GPT changed");
        struct stat old{},now{};
        require(::fstat(target.descriptor.get(),&old)==0 && ::fstat(current.descriptor.get(),&now)==0 && S_ISBLK(old.st_mode) &&
            old.st_rdev==now.st_rdev && storage_bytes(target.descriptor.get())==target.identity["bytes"].asUInt64(),"stale-device","Retained block descriptor no longer matches selected storage");
    }
}
} // namespace ure
