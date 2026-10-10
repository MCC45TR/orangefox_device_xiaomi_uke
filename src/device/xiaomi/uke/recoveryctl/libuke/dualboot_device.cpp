// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include "boot_state.hpp"
#include "dualboot_quarantine.hpp"
#include "dualboot_view_handoff.hpp"
#include "job_registry.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <linux/dm-ioctl.h>
#include <linux/fs.h>
#include <linux/magic.h>
#include <openssl/evp.h>
#include <set>
#include <sstream>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <utility>
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

namespace ure {
namespace {
constexpr std::uint64_t live_mib=1048576,live_maximum=1024ULL*1024*1024*1024;
constexpr std::size_t dm_budget=64*1024;
struct alignas(std::uint64_t) DmBuffer {
    std::array<char,dm_budget> bytes{};
    char* data() { return bytes.data(); }
    const char* data() const { return bytes.data(); }
    constexpr std::size_t size() const { return dm_budget; }
    auto begin() { return bytes.begin(); }
    auto end() { return bytes.end(); }
    ~DmBuffer() { volatile char* p=bytes.data(); for(std::size_t i=0;i<bytes.size();++i)p[i]=0; }
};
std::string live_seal(Value value,const char* member="plan_sha256") { value.removeMember(member); return sha256(json(value)); }
void discard_secret(std::string& value) noexcept {
    volatile char* bytes=value.data(); for(std::size_t i=0;i<value.size();++i)bytes[i]=0; value.clear();
}
std::string trimmed(std::string value) { while(!value.empty() && (value.back()=='\n' || value.back()=='\r'))value.pop_back(); return value; }
std::string live_property(const char* key) {
#ifdef __ANDROID__
    std::array<char,PROP_VALUE_MAX> value{}; __system_property_get(key,value.data()); return value.data();
#else
    (void)key; return {};
#endif
}
std::uint64_t decimal64(std::string_view text) {
    std::uint64_t out=0; const auto value=std::from_chars(text.data(),text.data()+text.size(),out);
    require(!text.empty() && text.size()<=20 && value.ec==std::errc() && value.ptr==text.data()+text.size(),"invalid-dualboot-device","Invalid unsigned kernel field"); return out;
}
std::string devtext(dev_t dev) { return std::to_string(major(dev))+":"+std::to_string(minor(dev)); }
dev_t devnumber(const std::string& value) {
    const auto colon=value.find(':'); require(colon!=value.npos,"invalid-dualboot-device","Missing kernel device number");
    const auto a=decimal64(value.substr(0,colon)),b=decimal64(value.substr(colon+1));
    require(a<=UINT32_MAX && b<=UINT32_MAX,"invalid-dualboot-device","Excessive kernel device number");
    const auto dev=makedev(static_cast<unsigned>(a),static_cast<unsigned>(b));
    require(major(dev)==a && minor(dev)==b,"invalid-dualboot-device","Kernel device number cannot be represented"); return dev;
}
void bounds(std::uint64_t offset,std::uint64_t bytes,std::uint64_t capacity) {
    require(bytes>0 && capacity<=live_maximum && offset<=capacity && bytes<=capacity-offset && offset%4096==0 && bytes%4096==0,
        "invalid-dualboot-range","The write range must be aligned and contained in its reviewed capacity");
}
void formatter_row(const Value& row) {
    require(row.isObject() && row["role"].isString() && row["filesystem"].isString() && row["bytes"].isUInt64() &&
        row["enabled"].isBool() && row["enabled"].asBool()==(row["bytes"].asUInt64()!=0),"invalid-dualboot-plan","Invalid bounded formatter allocation");
    const auto role=row["role"].asString(),type=row["filesystem"].asString(); const auto size=row["bytes"].asUInt64();
    require(role=="userdata" || role=="esp" || role=="linux_boot" || role=="linux" || role=="windows","invalid-dualboot-plan","Unknown bounded formatter role");
    if(row["enabled"]==false)return;
    require(size<=live_maximum && size%4096==0 &&
        ((role=="userdata" && type=="f2fs" && size>=4096*live_mib) || (role=="esp" && type=="fat32" && size>=512000000ULL) ||
        (role=="linux_boot" && type=="ext4" && size>=256*live_mib) || (role=="linux" && ((type=="ext4" && size>=32*live_mib) ||
        (type=="btrfs" && size>=256*live_mib) || (type=="f2fs" && size>=512*live_mib))) || (role=="windows" && type=="ntfs" && size>=32*live_mib)),
        "unsupported-live-filesystem","Live 4 KiB storage requires F2FS userdata and an ESP of at least 512 MB; selected filesystems must meet their formatter minimum");
}
void esp_bpb_policy(std::string_view header,std::uint64_t capacity) {
    require(header.size()==512,"dualboot-esp-readback","A complete FAT32 boot sector is required");
    auto field=[&](std::size_t offset,unsigned count){
        std::uint64_t out=0; for(unsigned i=0;i<count;++i)out|=std::uint64_t(static_cast<unsigned char>(header[offset+i]))<<(8*i); return out;
    };
    const auto sector=field(11,2),cluster=field(13,1),reserved=field(14,2),fats=field(16,1),total=field(32,4),fat_sectors=field(36,4),root=field(44,4);
    require(sector==4096 && capacity>=512000000ULL && total>0 && total<=capacity/sector && cluster>0 && cluster<=128 &&
        (cluster&(cluster-1))==0 && reserved>0 && (fats==1 || fats==2) && fat_sectors>0 &&
        field(17,2)==0 && field(19,2)==0 && field(22,2)==0 && field(42,2)==0 && field(510,2)==0xaa55 &&
        header.substr(82,8)=="FAT32   " && reserved+fats*fat_sectors<total,
        "dualboot-esp-readback","The formatted ESP is not a bounded 4 KiB FAT32 volume");
    const auto clusters=(total-reserved-fats*fat_sectors)/cluster;
    require(clusters>=65525 && clusters<0x0ffffff5ULL && (clusters+2)*4<=fat_sectors*sector && root>=2 && root<=clusters+1,
        "dualboot-esp-readback","The ESP data-cluster count, FAT capacity or root cluster is invalid for FAT32");
}
void device_plan(const Value& plan) {
    dualboot_validate_plan(plan);
    require(plan["schema"]==1 && plan["operation"]=="dualboot.setup" && identifier(plan["operation_id"].asString()) &&
        identifier(plan["firmware_profile"].asString()) && hash_valid(plan["plan_sha256"].asString()) && plan["plan_sha256"]==live_seal(plan),
        "invalid-dualboot-plan","Changed or invalid dualboot preview");
    const auto& id=plan["target_identity"]; const auto& gpt=plan["gpt"]; const auto& layout=gpt["layout"];
    require(id["kind"]=="live-block" && id["partition"]==false && id["logical_sector_bytes"].isUInt64() && id["logical_sector_bytes"].asUInt64()==4096 && id["bytes"].isUInt64() &&
        id["bytes"].asUInt64()>=64*1024*live_mib && id["bytes"].asUInt64()<=live_maximum && id["read_only_state"]==false &&
        id["unit_identity_available"]==true && hash_valid(id["unit_identity_sha256"].asString()) && uuid(id["disk_guid"].asString()) &&
        id["lun_address"].isString() && id["lun_address"].asString().ends_with(":0"),"unsupported-dualboot-device","Select the measured writable 4 KiB Uke UFS LUN 0");
    require(plan["data_loss"]==true && plan["confirmation_phrase"]=="ERASE USERDATA" && plan["request"]["userdata_policy"]=="recreate" &&
        plan["writes_other_partition_payloads"]==false && layout["pool"]["source"]=="ORIGINAL_USERDATA_ONLY" && layout["placement"]=="after_userdata" &&
        layout["userdata_policy"]=="recreate" && layout["advanced_record_edits"].isArray() && layout["advanced_record_edits"].empty(),
        "unsupported-live-dualboot-policy","This live backend implements explicit userdata erase and recreate only; OEM edits and preservation are excluded");
    require(gpt["operation"]=="gpt.layout" && gpt["plan_sha256"]==live_seal(gpt) && json(gpt["target_identity"])==json(id) &&
        json(layout["target_identity"])==json(id) && layout["layout_sha256"]==live_seal(layout,"layout_sha256") &&
        gpt["current_table"]["healthy"]==true && gpt["desired_table"]["healthy"]==true && gpt["current_table"]["disk_guid"]==id["disk_guid"] &&
        gpt["desired_table"]["disk_guid"]==id["disk_guid"] && layout["rows"].isArray() && layout["rows"].size()>=4 && layout["rows"].size()<=5,
        "invalid-dualboot-plan","Matching valid before/after GPTs and the exact target identity are required");
    const auto offset=layout["pool"]["offset"].asUInt64(),bytes=layout["pool"]["original_bytes"].asUInt64(); bounds(offset,bytes,id["bytes"].asUInt64());
    std::uint64_t at=offset; std::set<std::string> roles; unsigned enabled=0; Value old_data;
    const std::array<std::string,5> expected{"userdata","esp","linux_boot","linux","windows"}; unsigned previous=0;
    for(const auto& row:layout["rows"]) {
        const auto role=row["role"].asString(); const auto found=std::find(expected.begin(),expected.end(),role);
        require(found!=expected.end() && roles.insert(role).second,"invalid-dualboot-plan","Unknown or duplicated allocation role");
        const auto order=static_cast<unsigned>(found-expected.begin()); require(roles.size()==1 ? order==0 : order>previous,"invalid-dualboot-plan","Allocation order must be userdata, ESP, optional boot, Linux and Windows"); previous=order;
        require(row["offset"].isUInt64() && row["offset"].asUInt64()==at && row["bytes"].isUInt64() && row["enabled"].isBool() &&
            row["enabled"].asBool()==(row["bytes"].asUInt64()!=0),"invalid-dualboot-plan","Allocation cursor or enabled state differs");
        if(row["enabled"]==false)continue;
        const auto size=row["bytes"].asUInt64(); bounds(at,size,offset+bytes);
        require(row["start_lba"].asUInt64()==at/4096 && row["end_lba"].asUInt64()==(at+size)/4096-1 && row["index"].asUInt()>0 && uuid(row["partuuid"].asString()),
            "invalid-dualboot-plan","Allocation bytes differ from the reviewed GPT entry");
        formatter_row(row);
        if(role=="userdata") { old_data=row["previous"]; require(old_data["label"]=="userdata" && old_data["start_lba"]==row["start_lba"] &&
            old_data["index"]==row["index"] && old_data["partuuid"]==row["partuuid"] && old_data["bytes"].asUInt64()==bytes &&
            old_data["start_lba"].asUInt64()*4096==offset && old_data["attributes"].asUInt64()==0,
            "invalid-dualboot-plan","Userdata must retain its original start, entry, GUID and attributes"); }
        ++enabled; at+=size;
    }
    require(!old_data.isNull() && roles.contains("esp") && enabled>=2 &&
        (!plan["request"]["windows_enabled"].asBool() || plan["request"].get("esp_enabled",true).asBool()),
        "invalid-dualboot-plan","Userdata and at least one OS are required; Windows also requires ESP");
    for(const auto& record:layout["protected_records"]) {
        bool present=false; for(const auto& next:gpt["desired_table"]["partitions"])if(next["index"]==record["index"]) { require(!present && json(next)==json(record),"protected-record-changed","An OEM GPT record changed"); present=true; }
        if(!present)for(const auto& next:gpt["desired_table"]["reserved_records"])if(next["index"]==record["index"]) { require(!present && json(next)==json(record),"protected-record-changed","An OEM reservation changed"); present=true; }
        require(present,"protected-record-changed","An original protected record is missing");
    }
}
struct DmTable {
    std::string name,uuid,type,parameters; dev_t device=0; std::uint64_t length=0,start=0; int open_count=-1; std::uint32_t flags=0,event=0;
    DmTable()=default;
    DmTable(const DmTable&)=delete;
    DmTable& operator=(const DmTable&)=delete;
    DmTable(DmTable&&)=default;
    DmTable& operator=(DmTable&&)=delete;
    ~DmTable() { discard_secret(parameters); }
};
void dm_reply_header(const dm_ioctl& reply,std::size_t capacity) {
    // Header-only Linux DM replies end before data[7], at byte 305. The C
    // structure also contains trailing data/alignment storage (sizeof 312).
    // Table consumers separately require the aligned complete target payload.
    require(reply.version[0]==DM_VERSION_MAJOR && (reply.flags&DM_BUFFER_FULL_FLAG)==0 &&
        reply.data_size>=offsetof(dm_ioctl,data) && reply.data_size<=capacity,
        "invalid-dualboot-map","Truncated or unsupported device-mapper response");
}
class DmControl {
    Fd fd_;
    static DmBuffer request(const std::string& name,std::uint32_t flags=0) {
        require(name.empty() || (identifier(name) && name.size()<DM_NAME_LEN),"invalid-dualboot-map","Invalid owned mapper name");
        DmBuffer buffer{}; auto* io=reinterpret_cast<dm_ioctl*>(buffer.data()); io->version[0]=DM_VERSION_MAJOR;
        io->version[1]=0; io->version[2]=0; io->data_size=static_cast<std::uint32_t>(buffer.size()); io->data_start=sizeof(dm_ioctl); io->flags=flags;
        if(!name.empty())std::memcpy(io->name,name.data(),name.size());
        return buffer;
    }
    void call(unsigned long command,DmBuffer& buffer) const {
        auto* io=reinterpret_cast<dm_ioctl*>(buffer.data());
        require(::ioctl(fd_.get(),command,io)==0,"dualboot-mapper-error","The kernel rejected the bounded device-mapper operation");
        dm_reply_header(*io,buffer.size());
    }
public:
    explicit DmControl(const Root& system):fd_(system.open("dev/device-mapper",O_RDWR|O_NONBLOCK)) {
        struct stat st{}; require(::fstat(fd_.get(),&st)==0 && S_ISCHR(st.st_mode) && st.st_uid==0,"invalid-dualboot-map","Device-mapper control must be the root-owned kernel character device");
        const auto number=trimmed(system.read("sys/class/misc/device-mapper/dev",64)); require(st.st_rdev==devnumber(number),"invalid-dualboot-map","Mapper control identity differs from sysfs");
    }
    explicit DmControl(Fd injected):fd_(std::move(injected)) {} // Only a private native test directly constructs this overload.
    DmTable table(const std::string& name) const {
        auto buffer=request(name,DM_STATUS_TABLE_FLAG); call(DM_TABLE_STATUS,buffer); const auto* io=reinterpret_cast<const dm_ioctl*>(buffer.data());
        require(io->target_count==1 && (io->flags&DM_ACTIVE_PRESENT_FLAG)!=0 && (io->flags&(DM_SUSPEND_FLAG|DM_INACTIVE_PRESENT_FLAG))==0 &&
            io->data_start>=sizeof(dm_ioctl) && io->data_start%alignof(dm_target_spec)==0 && io->data_start<=io->data_size && sizeof(dm_target_spec)<io->data_size-io->data_start,
            "unsupported-dualboot-map","Exactly one active, unsuspended mapper target with no inactive replacement is required");
        const auto* spec=reinterpret_cast<const dm_target_spec*>(buffer.data()+io->data_start);
        const auto room=static_cast<std::size_t>(io->data_size-io->data_start-sizeof(dm_target_spec));
        const char* parameters=reinterpret_cast<const char*>(spec+1); const auto count=strnlen(parameters,room);
        require(count<room && strnlen(spec->target_type,sizeof(spec->target_type))<sizeof(spec->target_type) && strnlen(io->name,sizeof(io->name))<sizeof(io->name) &&
            strnlen(io->uuid,sizeof(io->uuid))<sizeof(io->uuid) && spec->sector_start==0 && spec->length>0 && spec->length<=live_maximum/512,
            "invalid-dualboot-map","Mapper table strings or sectors exceed the supported bounds");
        require(std::string_view(io->name)==name,"stale-dualboot-map","The mapper response name differs from the exact requested map");
        DmTable out; out.name=io->name; out.uuid=io->uuid; out.type=spec->target_type; out.parameters.assign(parameters,count); out.length=spec->length;
        out.start=spec->sector_start; out.device=static_cast<dev_t>(io->dev); out.open_count=io->open_count; out.flags=io->flags; out.event=io->event_nr;
        // Default-key / crypt table responses may contain keys on some kernels.
        // The complete ioctl buffer is never logged, returned as JSON or saved.
        std::fill(buffer.begin(),buffer.end(),0); return out;
    }
    DmTable create_linear(const std::string& name,const std::string& uuid_value,dev_t backing,std::uint64_t offset,std::uint64_t bytes) const {
        bounds(offset,bytes,live_maximum); require(uuid_value.size()<DM_UUID_LEN && uuid_value.starts_with("URE-DUALBOOT-"),"invalid-dualboot-map","An operation-bound private mapper UUID is required");
        auto buffer=request(name); auto* io=reinterpret_cast<dm_ioctl*>(buffer.data()); std::memcpy(io->uuid,uuid_value.data(),uuid_value.size()); call(DM_DEV_CREATE,buffer);
        try {
            buffer=request(name); io=reinterpret_cast<dm_ioctl*>(buffer.data()); io->target_count=1; auto* spec=reinterpret_cast<dm_target_spec*>(buffer.data()+io->data_start);
            spec->sector_start=0; spec->length=bytes/512; std::memcpy(spec->target_type,"linear",6);
            const auto parameters=devtext(backing)+" "+std::to_string(offset/512); std::memcpy(spec+1,parameters.c_str(),parameters.size()+1);
            const auto padded=(sizeof(dm_target_spec)+parameters.size()+1+7U)&~std::size_t(7U); spec->next=static_cast<std::uint32_t>(padded);
            io->data_size=io->data_start+static_cast<std::uint32_t>(padded); call(DM_TABLE_LOAD,buffer);
            buffer=request(name); call(DM_DEV_SUSPEND,buffer); auto out=table(name);
            require(out.uuid==uuid_value && out.type=="linear" && out.parameters==parameters && out.length==bytes/512 && out.open_count==0,
                "invalid-dualboot-map","Created mapper readback differs from the exact bounded table"); return out;
        } catch(...) { try { remove(name,uuid_value); } catch(...) {} throw; }
    }
    void remove(const std::string& name,const std::string& expected_uuid) const {
        auto buffer=request(name); call(DM_DEV_STATUS,buffer); const auto* io=reinterpret_cast<const dm_ioctl*>(buffer.data());
        require(strnlen(io->uuid,sizeof(io->uuid))<sizeof(io->uuid) && std::string(io->uuid)==expected_uuid && io->open_count==0,
            "busy-dualboot-map","Only the idle mapper with the operation's exact UUID may be removed");
        buffer=request(name); call(DM_DEV_REMOVE,buffer);
    }
};
struct CryptoPolicy { std::string type,cipher; dev_t backing=0; bool wrapped=false; std::uint64_t iv=0,offset=0; };
CryptoPolicy crypto_policy(DmTable& table,const Value& userdata) {
    std::vector<std::string> words;
    struct Secrets {
        std::string& table; std::vector<std::string>& tokens;
        ~Secrets() { discard_secret(table); for(auto& token:tokens)discard_secret(token); }
    } secrets{table.parameters,words};
    require(table.type=="default-key" && table.length==userdata["bytes"].asUInt64()/512 && table.open_count==0 &&
        (table.flags&DM_READONLY_FLAG)==0,"unsupported-userdata-encryption","An idle active full-length metadata-encryption map is required");
    std::size_t at=0;
    while((at=table.parameters.find_first_not_of(" \t\r\n",at))!=table.parameters.npos) {
        const auto end=table.parameters.find_first_of(" \t\r\n",at),length=(end==table.parameters.npos ? table.parameters.size() : end)-at;
        require(words.size()<24 && length<=4096,"unsupported-userdata-encryption","Oversized metadata-encryption table");
        words.emplace_back(table.parameters,at,length); if(end==table.parameters.npos)break; at=end;
    }
    require(words.size()>=5,"unsupported-userdata-encryption","Incomplete metadata-encryption target");
    CryptoPolicy out; out.type=table.type; out.cipher=words[0]; out.iv=decimal64(words[2]); out.backing=devnumber(words[3]); out.offset=decimal64(words[4]);
    // Discard the key field immediately. It is never used to create or reload a
    // crypto table: all formatting uses a bounded child of the existing map.
    discard_secret(words[1]); discard_secret(table.parameters);
    require(out.backing==devnumber(userdata["device_number"].asString()) && out.iv==0 && out.offset==0 &&
        (out.cipher=="aes-xts-plain64" || out.cipher=="AES-256-XTS"),"unsupported-userdata-encryption","Metadata encryption must start at sector zero of this exact userdata partition");
    if(words.size()>5) {
        const auto count=decimal64(words[5]); require(count==words.size()-6,"unsupported-userdata-encryption","Encryption option count differs");
        std::set<std::string> seen; for(std::size_t i=6;i<words.size();++i) {
            require(seen.insert(words[i]).second,"unsupported-userdata-encryption","Duplicated encryption option");
            require(words[i]=="allow_discards" || words[i]=="sector_size:4096" || words[i]=="iv_large_sectors" || words[i]=="wrappedkey_v0" ||
                words[i]=="set_dun" || words[i]=="iv_offset:0", "unsupported-userdata-encryption","Unknown metadata-encryption option");
            out.wrapped=out.wrapped || words[i]=="wrappedkey_v0";
        }
        require(seen.contains("sector_size:4096") && (table.type!="crypt" || seen.contains("iv_large_sectors")),
            "unsupported-userdata-encryption","The installed policy requires 4 KiB encryption sectors");
    } else throw Error("unsupported-userdata-encryption","The installed wrapped-key options are unavailable");
    require(out.wrapped,"unsupported-userdata-encryption","The installed metadata-encryption policy requires a wrapped key"); return out;
}
Fd node(const Root& system,const Value& object,bool write,bool exclusive=false,bool wait_owned=false) {
    const auto name=object["kernel_name"].asString(); require(identifier(name),"invalid-dualboot-device","Invalid canonical device name");
    require(!wait_owned || name.starts_with("dm-"),"invalid-dualboot-map","Only an operation-owned mapper view may wait for its kernel node");
    Fd result; const auto start=monotonic_ms();
    do {
        for(const auto* prefix:{"dev/block/","dev/"})try { result=system.open(std::string(prefix)+name,(write ? O_RDWR : O_RDONLY)|O_NONBLOCK|(exclusive ? O_EXCL : 0)); break; }
            catch(const Error& error) { if(error.code!="path-unavailable")throw; }
        if(result.get()>=0 || !wait_owned || monotonic_ms()-start>=2000)break;
        // ueventd publishes dm-N asynchronously. Waiting never substitutes a
        // pathname for the descriptor/device/capacity checks below.
        ::usleep(10000);
    } while(true);
    struct stat st{}; require(result.get()>=0 && ::fstat(result.get(),&st)==0 && S_ISBLK(st.st_mode) && st.st_rdev==devnumber(object["device_number"].asString()) &&
        storage_bytes(result.get())==object["bytes"].asUInt64(),"stale-dualboot-device","Canonical block descriptor differs from the measured object"); return result;
}
void recovery_bcb_command(std::string_view command) {
    // AOSP bootloader_message places command[32] at misc offset zero. Init's
    // reboot,recovery path writes misc if this field is empty; that write is
    // deliberately unavailable under the shared legacy write policy. Admit
    // destructive setup only with an already persisted exact recovery request.
    constexpr std::string_view expected="boot-recovery";
    require(command.size()==32 && command.substr(0,expected.size())==expected &&
        std::all_of(command.begin()+static_cast<std::ptrdiff_t>(expected.size()),command.end(),[](char byte){return byte=='\0';}),
        "recovery-bcb-command-required","Repartitioning requires an existing exact boot-recovery BCB command; this operation never writes misc");
}
void recovery_bcb_read(int descriptor) {
    std::string command;
    try { command=storage_read(descriptor,0,32); }
    catch(const Error&) { throw Error("recovery-bcb-unavailable","The existing recovery BCB command could not be completely read"); }
    recovery_bcb_command(command);
}
Value recovery_misc_object(const Value& graph) {
    require(graph["objects"].isArray(),"recovery-bcb-unavailable","Kernel block inventory is unavailable");
    Value selected;
    for(const auto& object:graph["objects"])if(object["label"]=="misc") {
        require(selected.isNull(),"recovery-bcb-unavailable","The kernel inventory contains more than one misc partition"); selected=object;
    }
    require(selected.isObject() && selected["partition"]==true && selected["kernel_name"].isString() && identifier(selected["kernel_name"].asString()) &&
        selected["sysfs_path"].isString() && selected["sysfs_path"].asString().starts_with("sys/devices/") &&
        selected["device_number"].isString() && selected["partuuid"].isString() && uuid(selected["partuuid"].asString()) &&
        selected["bytes"].isUInt64() && selected["bytes"].asUInt64()>=2048 && selected["bytes"].asUInt64()<=live_maximum &&
        selected["dependencies_available"]==true && selected["holders"].isArray() && selected["holders"].empty() &&
        selected["slaves"].isArray() && selected["slaves"].empty(),
        "recovery-bcb-unavailable","An unambiguous kernel-identified misc partition without mapper dependencies is required");
    (void)devnumber(selected["device_number"].asString()); return selected;
}
void recovery_misc_geometry(const Value& object,const Value& plan) {
    Value record;
    for(const auto& candidate:plan["gpt"]["layout"]["protected_records"])if(candidate["label"]=="misc") {
        require(record.isNull(),"recovery-bcb-unavailable","More than one protected misc record is present"); record=candidate;
    }
    require(record.isObject() && object["parent_lun_sysfs"]==plan["target_identity"]["sysfs_path"] &&
        object["bytes"].isUInt64() && record["bytes"].isUInt64() && object["bytes"].asUInt64()==record["bytes"].asUInt64() && object["partuuid"]==record["partuuid"] &&
        object["partition_index"].isUInt64() && object["partition_index"].asUInt64()==record["index"].asUInt64() &&
        object["start_512_sectors"].isUInt64() && record["start_lba"].isUInt64() && record["start_lba"].asUInt64()<=live_maximum/4096 &&
        object["start_512_sectors"].asUInt64()==record["start_lba"].asUInt64()*8,
        "recovery-bcb-unavailable","The kernel misc partition must match its unchanged protected GPT record on the reviewed UFS LUN");
}
Value recovery_misc(const Root& system,const Value& plan) {
    auto object=recovery_misc_object(storage_graph(system)); recovery_misc_geometry(object,plan); return object;
}
class RecoveryBcb {
    Value object_;
    Fd descriptor_;
public:
    RecoveryBcb(const Root& system,const Value& plan):object_(recovery_misc(system,plan)),descriptor_(node(system,object_,false)) { verify(system); }
    void verify(const Root& system) const {
        const auto current=recovery_misc_object(storage_graph(system));
        for(const auto* field:{"kernel_name","sysfs_path","device_number","partuuid","bytes","partition","parent_lun_sysfs","partition_index","start_512_sectors"})
            require(json(current[field])==json(object_[field]),"recovery-bcb-changed","The retained misc partition identity changed");
        struct stat st{};
        require(::fstat(descriptor_.get(),&st)==0 && S_ISBLK(st.st_mode) && st.st_rdev==devnumber(object_["device_number"].asString()) &&
            storage_bytes(descriptor_.get())==object_["bytes"].asUInt64(),"recovery-bcb-changed","The retained read-only misc descriptor differs from the observed partition");
        recovery_bcb_read(descriptor_.get());
    }
};
Value original_userdata(const Value& graph,const Value& plan) {
    Value selected; const auto& pool=plan["gpt"]["layout"]["pool"];
    for(const auto& item:graph["objects"])if(item["label"]=="userdata" && item["parent_lun_sysfs"]==plan["target_identity"]["sysfs_path"]) {
        require(selected.isNull(),"ambiguous-userdata","Duplicated userdata partition"); selected=item;
    }
    require(selected.isObject() && selected["partition"]==true && selected["start_512_sectors"].asUInt64()*512==pool["offset"].asUInt64() &&
        selected["bytes"].isUInt64() && pool["original_bytes"].isUInt64() && selected["bytes"].asUInt64()==pool["original_bytes"].asUInt64() && selected["dependencies_available"]==true,
        "userdata-node-geometry-changed","This execution requires the original userdata kernel partition; refresh or restore GPT and reboot before a new plan"); return selected;
}
Value encryption_object(const Value& graph,const Value& userdata) {
    require(userdata["holders"].isArray() && userdata["holders"].size()==1,"unsupported-userdata-encryption","Exactly one existing encryption holder is required");
    Value selected; for(const auto& item:graph["objects"])if(item["kernel_name"]==userdata["holders"][0]) { require(selected.isNull(),"ambiguous-userdata","Duplicated encryption holder"); selected=item; }
    require(selected.isObject() && selected["mapper_name"].isString() && identifier(selected["mapper_name"].asString()) && selected["slaves"].size()==1 &&
        selected["slaves"][0]==userdata["kernel_name"] && selected["holders"].isArray() && selected["holders"].empty() && selected["mounts"].empty() && selected["bytes"]==userdata["bytes"],
        "unsupported-userdata-encryption","Encryption must be a direct, unmounted userdata mapping without additional holders"); return selected;
}
bool mount_option(const std::string& options,std::string_view wanted) {
    std::size_t at=0;
    while(at<=options.size()) {
        const auto end=options.find(',',at),length=(end==options.npos ? options.size() : end)-at;
        if(std::string_view(options).substr(at,length)==wanted)return true;
        if(end==options.npos)break;
        at=end+1;
    }
    return false;
}
void metadata_mount_policy(const Value& mount,const std::string& number) {
    const auto options=mount["options"].asString(),super=mount["super_options"].asString();
    require(mount["device_number"]==number && mount["path"]=="/metadata" && mount["filesystem"]=="f2fs" &&
        mount_option(options,"ro") && !mount_option(options,"rw") && mount_option(super,"ro") && !mount_option(super,"rw") &&
        mount_option(super,"norecovery"),
        "metadata-not-readonly-norecovery","Snapshot inspection requires this exact metadata partition mounted F2FS read-only with roll-forward recovery disabled in every visible namespace");
}
Value metadata_object(const Value& graph,const Value& plan) {
    Value metadata,record;
    for(const auto& item:graph["objects"])if(item["label"]=="metadata" && item["parent_lun_sysfs"]==plan["target_identity"]["sysfs_path"]) {
        require(metadata.isNull(),"ambiguous-metadata","Duplicated metadata device"); metadata=item;
    }
    for(const auto& item:plan["gpt"]["layout"]["protected_records"])if(item["label"]=="metadata") {
        require(record.isNull(),"ambiguous-metadata","Duplicated protected metadata record"); record=item;
    }
    require(metadata.isObject() && record.isObject() && metadata["partition"]==true && metadata["dependencies_available"]==true &&
        metadata["holders"].isArray() && metadata["holders"].empty() && metadata["slaves"].isArray() && metadata["slaves"].empty() &&
        metadata["bytes"].isUInt64() && record["bytes"].isUInt64() && metadata["bytes"].asUInt64()==record["bytes"].asUInt64() && metadata["start_512_sectors"].asUInt64()==record["start_lba"].asUInt64()*8 &&
        metadata["mounts"].isArray() && metadata["mounts"].size()==1 && metadata["mounts"][0]["path"]=="/metadata",
        "metadata-mount-unavailable","The exact protected metadata partition must already have one /metadata mount; this operation never mounts or remounts it");
    return metadata;
}
Value mounted_metadata(const Root& system,const Value& plan) {
    const auto selected=metadata_object(storage_graph(system),plan); const auto number=selected["device_number"].asString();
    // This fd binds reads to the selected mount. A pathname that merely exists
    // in the ramdisk cannot substitute for an unmounted metadata filesystem.
    auto directory=system.open("metadata",O_RDONLY|O_DIRECTORY); struct stat st{}; struct statfs fsinfo{};
    require(::fstat(directory.get(),&st)==0 && st.st_dev==devnumber(number) && ::fstatfs(directory.get(),&fsinfo)==0 &&
        fsinfo.f_type==F2FS_SUPER_MAGIC && (fsinfo.f_flags&ST_RDONLY)!=0,"metadata-mount-unavailable","The /metadata descriptor is not the measured read-only F2FS filesystem");
    std::istringstream input(system.read("proc/"+std::to_string(::getpid())+"/mountinfo",4*1024*1024)); std::string line; unsigned matches=0;
    while(std::getline(input,line)) {
        std::istringstream row(line); std::vector<std::string> words; std::string word; while(row>>word)words.push_back(word);
        const auto separator=std::find(words.begin(),words.end(),"-");
        require(words.size()>=10 && separator-words.begin()>=6 && words.end()-separator==4,"metadata-mount-unavailable","Malformed kernel mount table");
        if(words[4]!="/metadata" && words[2]!=number)continue;
        require(words[3]=="/" && ++matches==1,"metadata-mount-unavailable","Metadata must have one exact filesystem-root mount");
        Value observation; observation["device_number"]=words[2]; observation["path"]=words[4]; observation["options"]=words[5];
        observation["filesystem"]=*(separator+1); observation["super_options"]=*(separator+3); metadata_mount_policy(observation,number);
    }
    require(matches==1,"metadata-mount-unavailable","No authoritative read-only norecovery metadata mount was observed"); return selected;
}
void device_usage_policy(const Value& usage,const Value& metadata) {
    require(usage["blockers"].isArray() && usage["blockers_truncated"]==false && usage["blocker_count"].asUInt()==usage["blockers"].size(),
        "dualboot-device-busy","Complete untruncated ownership observations are required");
    for(const auto* category:{"mounts","processes","swaps","usb"})require(usage["coverage"][category]==true,
        "dualboot-device-busy","Mount, process, swap and USB ownership must be observable");
    for(const auto& blocker:usage["blockers"]) {
        if(blocker["code"]=="mounted")require(blocker["detail"]["path"]=="/metadata","dualboot-device-busy","Another partition is mounted");
        else if(blocker["code"]=="mounts")metadata_mount_policy(blocker["detail"],metadata["device_number"].asString());
        else throw Error("dualboot-device-busy","A writer, swap, USB export or unresolved dependency prevents userdata recreation");
    }
}
void device_usage(const Root& system,const Value& plan) {
    const auto metadata=mounted_metadata(system,plan);
    device_usage_policy(storage_usage(system,plan["target_identity"]["stable_id"].asString()),metadata);
}
void check_snapshot(const Root& system,const Value& plan) {
    const auto metadata=mounted_metadata(system,plan); Root metadata_root(system.open("metadata",O_RDONLY|O_DIRECTORY));
    struct stat metadata_stat{}; require(::fstat(metadata_root.fd(),&metadata_stat)==0 && metadata_stat.st_dev==devnumber(metadata["device_number"].asString()),
        "metadata-mount-unavailable","Metadata changed before OTA inspection");
    for(const auto* command:{"get-number-slots","get-current-slot","get-snapshot-merge-status"}) {
        const auto result=run_tool("bootctl",{command},15); require(result.status==0 && !result.timed_out,"unknown-android-state","The current boot-control HAL did not provide an authoritative state");
        const auto value=trimmed(result.output);
        if(std::string_view(command)=="get-number-slots")require(value=="2","unknown-android-state","Exactly two slots are required");
        else if(std::string_view(command)=="get-current-slot")require((value=="0" && live_property("ro.boot.slot_suffix")=="_a") ||
            (value=="1" && live_property("ro.boot.slot_suffix")=="_b"),"unknown-android-state","Boot-control and boot-property slots differ");
        else require(value=="none","android-snapshot-active","Unknown, pending, merging or cancelled Virtual A/B updates block userdata recreation");
    }
    for(const auto* directory:{"ota","ota/snapshots"})if(metadata_root.exists(directory)) {
        for(const auto& name:metadata_root.list(directory,4096))require(name=="snapshots" && metadata_root.list(std::string(directory)+"/"+name,4096).empty(),
            "android-snapshot-active","Persistent OTA state is present; reconcile it through Android before repartitioning");
    }
    const auto graph=storage_graph(system); for(const auto& object:graph["objects"]) {
        const auto name=object["mapper_name"].asString(),uuid_value=object["mapper_uuid"].asString();
        require(name.find("-cow")==name.npos && name.find("-snap")==name.npos && name.find("snapshot")==name.npos &&
            uuid_value.find("snapshot")==uuid_value.npos,"android-snapshot-active","A snapshot/COW mapper is present");
    }
}
void current_root(const Root& system) {
    Root current("/"); require(json(descriptor_identity(current.fd()))==json(descriptor_identity(system.fd())) && ::geteuid()==0,
        "untrusted-dualboot-system","Live admission requires the current recovery root and root privileges");
    require(bootloader_unlocked(system),"unsupported-dualboot-device",
        "Current kernel and recovery properties must consistently identify unlocked Uke");
}
void crypto_fstab(const Root& system) {
    bool matched=false; for(const auto* file:{"vendor/etc/fstab.qcom","first_stage_ramdisk/fstab.qcom","fstab.qcom"})if(system.exists(file)) {
        std::istringstream text(system.read(file,256*1024)); std::string line;
        while(std::getline(text,line)) {
            if(line.empty() || line.front()=='#')continue;
            std::istringstream row(line); std::array<std::string,5> field; std::string extra;
            if(!(row>>field[0]>>field[1]>>field[2]>>field[3]>>field[4]) || row>>extra)continue;
            if(field[1]!="/data")continue;
            require(field[0].ends_with("/userdata") && field[2]=="f2fs" && mount_option(field[3],"inlinecrypt") &&
                mount_option(field[4],"fileencryption=aes-256-xts:aes-256-cts:v2+inlinecrypt_optimized+wrappedkey_v0") &&
                mount_option(field[4],"metadata_encryption=aes-256-xts:wrappedkey_v0") &&
                mount_option(field[4],"keydirectory=/metadata/vold/metadata_encryption"),
                "unsupported-userdata-policy","The installed F2FS, FBE and wrapped metadata-key policy differs from this backend"); matched=true;
        }
    }
    require(matched,"userdata-policy-unavailable","The current installed userdata encryption fstab is unavailable");
}
std::string range_digest(int fd,std::uint64_t offset,std::uint64_t bytes) {
    require(offset<=INT64_MAX && bytes<=static_cast<std::uint64_t>(INT64_MAX)-offset,"invalid-dualboot-range","Digest exceeds signed offsets");
    using Digest=std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)>; Digest context(EVP_MD_CTX_new(),EVP_MD_CTX_free);
    require(context && EVP_DigestInit_ex(context.get(),EVP_sha256(),nullptr)==1,"hash-error","Cannot create a protected-range digest");
    for(std::uint64_t at=0;at<bytes;) { const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(live_mib,bytes-at));
        const auto data=storage_read(fd,offset+at,count); require(EVP_DigestUpdate(context.get(),data.data(),data.size())==1,"hash-error","Cannot update the protected digest"); at+=count; }
    std::array<unsigned char,EVP_MAX_MD_SIZE> value{}; unsigned size=0; require(EVP_DigestFinal_ex(context.get(),value.data(),&size)==1 && size==32,"hash-error","Cannot complete the protected digest");
    static constexpr char hex[]="0123456789abcdef"; std::string out; for(unsigned i=0;i<size;++i) { out+=hex[value[i]>>4]; out+=hex[value[i]&15]; } return out;
}
std::vector<std::pair<std::uint64_t,std::uint64_t>> untouched_ranges(const Value& plan) {
    std::vector<std::pair<std::uint64_t,std::uint64_t>> writes;
    const auto& pool=plan["gpt"]["layout"]["pool"]; writes.emplace_back(pool["offset"].asUInt64(),pool["offset"].asUInt64()+pool["original_bytes"].asUInt64());
    require(plan["gpt"]["after"].isArray() && plan["gpt"]["after"].size()==5 && plan["gpt"]["before"].size()==5,"invalid-dualboot-plan","Both five-region GPT descriptions are required");
    std::set<std::string> names;
    for(const auto& item:plan["gpt"]["after"]) {
        const auto name=item["name"].asString(); require(names.insert(name).second && (name=="primary_table" || name=="primary_header" || name=="backup_table" || name=="backup_header" || name=="protective_mbr"),"invalid-dualboot-plan","Invalid GPT region");
        const auto start=item["offset"].asUInt64(),bytes=item["bytes"].asUInt64(); bounds(start,bytes,plan["target_identity"]["bytes"].asUInt64());
        require(bytes<=4*live_mib && hash_valid(item["sha256"].asString()),"invalid-dualboot-plan","Invalid GPT metadata size or digest");
        Value before; for(const auto& old:plan["gpt"]["before"])if(old["name"]==name) { require(before.isNull(),"invalid-dualboot-plan","Duplicated original region"); before=old; }
        require(before.isObject() && before["offset"]==item["offset"] && before["bytes"]==item["bytes"] && hash_valid(before["sha256"].asString()),"invalid-dualboot-plan","Before and after GPT range boundaries must match");
        writes.emplace_back(start,start+bytes);
    }
    std::sort(writes.begin(),writes.end()); std::uint64_t cursor=0; std::vector<std::pair<std::uint64_t,std::uint64_t>> out;
    for(const auto& [start,end]:writes) { require(cursor<=start,"invalid-dualboot-plan","GPT metadata overlaps userdata or another GPT range"); if(cursor<start)out.emplace_back(cursor,start-cursor); cursor=end; }
    const auto capacity=plan["target_identity"]["bytes"].asUInt64(); require(cursor<=capacity,"invalid-dualboot-plan","Metadata exceeds the target"); if(cursor<capacity)out.emplace_back(cursor,capacity-cursor); return out;
}
Value protected_digests(int fd,const Value& plan) {
    Value out(Json::arrayValue); for(const auto& [offset,bytes]:untouched_ranges(plan)) { Value row; row["offset"]=Json::UInt64(offset); row["bytes"]=Json::UInt64(bytes); row["sha256"]=range_digest(fd,offset,bytes); out.append(row); } return out;
}
void protected_verify(int fd,const Value& plan,const Value& manifest) {
    const auto ranges=untouched_ranges(plan); require(manifest.isArray() && manifest.size()==ranges.size(),"invalid-dualboot-journal","Protected range coverage differs");
    for(std::size_t i=0;i<ranges.size();++i) { const auto& row=manifest[static_cast<Json::ArrayIndex>(i)]; require(row["offset"].asUInt64()==ranges[i].first && row["bytes"].asUInt64()==ranges[i].second &&
        hash_valid(row["sha256"].asString()) && range_digest(fd,ranges[i].first,ranges[i].second)==row["sha256"].asString(),"protected-payload-changed","A byte outside original userdata and GPT metadata changed; writes are blocked"); }
}
void sync_fd(int fd) { require(::fsync(fd)==0,"dualboot-sync-error","Cannot durably synchronize the operation"); }
void write_exact(int fd,std::uint64_t offset,const std::string& data) {
    require(offset<=INT64_MAX && data.size()<=static_cast<std::uint64_t>(INT64_MAX)-offset,"invalid-dualboot-range","Write offset exceeds signed capacity");
    std::size_t done=0; while(done<data.size()) { const auto n=::pwrite(fd,data.data()+done,std::min<std::size_t>(4096,data.size()-done),static_cast<off_t>(offset+done));
        if(n<0 && errno==EINTR)continue;
        require(n>0,"dualboot-write-error","Bounded metadata write failed"); done+=static_cast<std::size_t>(n); }
}
void journal_usb(const Root& system,const Root& store) {
    struct stat st{}; struct statvfs space{}; require(::fstat(store.fd(),&st)==0 && ::fstatvfs(store.fd(),&space)==0 && st.st_uid==0 &&
        (st.st_mode&0777)==0700 && (space.f_flag&ST_RDONLY)==0 && space.f_frsize>0 && space.f_bavail>=(32*live_mib-1)/space.f_frsize+1,
        "unsafe-dualboot-journal","Use a private root-owned writable external USB directory with at least 32 MiB free");
    const auto graph=storage_graph(system);
    Value object; for(const auto& item:graph["objects"])if(item["device_number"]==devtext(st.st_dev)) { require(object.isNull(),"unsafe-dualboot-journal","Ambiguous journal storage"); object=item; }
    require(object.isObject() && object["sysfs_path"].isString(),"unsafe-dualboot-journal","Journal must reside on a measured block filesystem");
    bool usb=false; auto path=fs::path(object["sysfs_path"].asString());
    while(path.generic_string().starts_with("sys/devices/")) {
        try { const auto link=system.link((path/"subsystem").generic_string()); if(fs::path(link).filename()=="usb")usb=true; }
        catch(const Error& error) { if(error.code!="path-unavailable")throw; }
        path=path.parent_path();
    }
    require(usb,"unsafe-dualboot-journal","Journal storage must be connected through a kernel USB device ancestor"); sync_fd(store.fd());
}
class Quarantine {
    Root directory_;
    Value intent_;
    const RuntimeActivityLease& lease_;
    std::string marker_;
    void publish_marker(DualbootQuarantineState state) {
        require(lease_.valid(),"dualboot-runtime-lease-lost","The recovery runtime lease changed");
        const auto* contents=dualboot_quarantine_marker(state);
        require(contents!=nullptr,"dualboot-quarantine-error","Invalid quarantine checkpoint");
        if(marker_.empty()) {
            auto file=directory_.open("phase",O_WRONLY|O_CREAT|O_EXCL,0600); write_exact(file.get(),0,contents); sync_fd(file.get()); sync_fd(directory_.fd());
        } else directory_.atomic_save("phase",contents,sha256(marker_));
        marker_=contents;
        require(directory_.read("phase",96)==marker_ && dualboot_quarantine_state()==state,"dualboot-quarantine-error","Recovery quarantine marker readback differs");
    }
public:
    Quarantine(const Root& system,const StorageTarget& target,const Value& plan,const Root& store,const fs::path& journal,bool create,const RuntimeActivityLease& lease)
        :directory_(private_directory(dualboot_quarantine_detail::path(),create)),lease_(lease) {
        struct stat st{}; require(::fstat(store.fd(),&st)==0,"dualboot-quarantine-error","Cannot bind the durable journal");
        if(create) {
            intent_["schema"]=1; intent_["format"]="ure-dualboot-quarantine"; intent_["plan_sha256"]=plan["plan_sha256"];
            intent_["boot_id_sha256"]=sha256(trimmed(system.read("proc/sys/kernel/random/boot_id",256)));
            intent_["unit_identity_sha256"]=target.identity["unit_identity_sha256"]; intent_["lun_stable_id"]=target.identity["stable_id"];
            intent_["journal_path"]=fs::absolute(journal).lexically_normal().string(); intent_["journal_device"]=Json::UInt64(st.st_dev); intent_["journal_inode"]=Json::UInt64(st.st_ino);
            intent_["alias_device_numbers"]=storage_usage(system,target.identity["stable_id"].asString())["related_device_numbers"];
            intent_["phase"]="BEFORE_ERASE"; intent_["irreversible_data_loss"]=false; directory_.save_record("intent.json",intent_);
            publish_marker(DualbootQuarantineState::Pending);
        } else {
            intent_=parse_json(directory_.read("intent.json",1024*1024));
            require(intent_["schema"]==1 && intent_["format"]=="ure-dualboot-quarantine" && intent_["plan_sha256"]==plan["plan_sha256"] &&
                intent_["boot_id_sha256"]==sha256(trimmed(system.read("proc/sys/kernel/random/boot_id",256))) &&
                intent_["unit_identity_sha256"]==target.identity["unit_identity_sha256"] && intent_["journal_device"].asUInt64()==st.st_dev &&
                intent_["journal_inode"].asUInt64()==st.st_ino,"dualboot-quarantine-error","Another plan or replaced journal owns the pending quarantine");
            marker_=directory_.read("phase",96);
            require(dualboot_quarantine_state()!=DualbootQuarantineState::Invalid && dualboot_quarantine_state()!=DualbootQuarantineState::Clear,
                "dualboot-quarantine-error","Pending quarantine marker is invalid");
        }
        verify();
    }
    void verify() const { require(lease_.valid() && json(parse_json(directory_.read("intent.json",1024*1024)))==json(intent_) && directory_.read("phase",96)==marker_,
        "dualboot-quarantine-error","Pending ownership intent or runtime lease changed"); sync_fd(directory_.fd()); }
    void checkpoint(const Value& state) {
        verify(); intent_["phase"]=state["phase"]; intent_["irreversible_data_loss"]=state.get("irreversible_data_loss",false);
        directory_.save_record("intent.json",intent_,true);
        const auto next=intent_["phase"]=="COMMITTED_REBOOT_REQUIRED" ? DualbootQuarantineState::Committed :
            intent_["phase"]=="GPT_RESTORED_REBOOT_REQUIRED" ? DualbootQuarantineState::GptRestored : DualbootQuarantineState::Pending;
        publish_marker(next); verify();
    }
};
void phase(const Root& store,Value& state,const std::string& name,Quarantine* pending=nullptr) {
    state["phase"]=name; state["timestamp_utc"]=utc(); store.save_record("state.json",state,true); sync_fd(store.fd());
    if(pending)pending->checkpoint(state);
}
Value read_record(const Root& store,const std::string& name) {
    auto file=store.open(name,O_RDONLY|O_NONBLOCK); struct stat st{};
    require(::fstat(file.get(),&st)==0 && S_ISREG(st.st_mode) && st.st_uid==0 && st.st_nlink==1 && (st.st_mode&0777)==0600 && st.st_size>=0 && st.st_size<=4*1024*1024,
        "invalid-dualboot-journal","Private journal record has unexpected metadata"); return parse_json(store.read(name,4*1024*1024));
}
std::string journal_bytes(const Root& store,const std::string& name,std::size_t bytes,const std::string& hash) {
    auto file=store.open(name,O_RDONLY|O_NONBLOCK); struct stat st{}; require(::fstat(file.get(),&st)==0 && S_ISREG(st.st_mode) && st.st_uid==0 &&
        st.st_nlink==1 && (st.st_mode&0777)==0600 && st.st_size>=0 && static_cast<std::uint64_t>(st.st_size)==bytes,"invalid-dualboot-journal","GPT backup is not the exact private regular file");
    const auto out=storage_read(file.get(),0,bytes); require(sha256(out)==hash,"invalid-dualboot-journal","GPT backup hash differs"); return out;
}
struct Region { std::string name,before,after; std::uint64_t offset=0; };
unsigned region_order(const std::string& name) {
    if(name=="backup_table")return 0;
    if(name=="backup_header")return 1;
    if(name=="primary_table")return 2;
    if(name=="primary_header")return 3;
    require(name=="protective_mbr","invalid-dualboot-plan","Unknown GPT region"); return 4;
}
std::vector<Region> load_regions(const Root& store,const Value& plan) {
    std::vector<Region> result; for(const auto& row:plan["gpt"]["after"]) {
        Value old; for(const auto& before:plan["gpt"]["before"])if(before["name"]==row["name"])old=before;
        const auto name=row["name"].asString(); result.push_back({name,journal_bytes(store,"before-"+name+".bin",old["bytes"].asUInt(),old["sha256"].asString()),
            journal_bytes(store,"after-"+name+".bin",row["bytes"].asUInt(),row["sha256"].asString()),row["offset"].asUInt64()});
    }
    std::sort(result.begin(),result.end(),[](const Region& a,const Region& b){return region_order(a.name)<region_order(b.name);}); return result;
}
Value region_inspect(int fd,const std::vector<Region>& regions,const Value& state) {
    Value out(Json::arrayValue); for(const auto& region:regions) {
        const auto bytes=storage_read(fd,region.offset,region.before.size()); Value item; item["name"]=region.name;
        item["before"]=bytes==region.before; item["after"]=bytes==region.after;
        const bool recorded=(state["phase"]=="GPT_WRITE" || state["phase"]=="GPT_RESTORE") && state["region"]==region.name;
        bool interrupted=recorded && region.before.size()==region.after.size();
        for(std::size_t i=0;interrupted && i<bytes.size();++i)interrupted=bytes[i]==region.before[i] || bytes[i]==region.after[i];
        item["interrupted_region_allowed"]=interrupted;
        require(item["before"]==true || item["after"]==true || interrupted,"foreign-gpt-change","GPT bytes contain a foreign value or differ outside the recorded in-progress write"); out.append(item);
    } return out;
}
template<class Checkpoint,class Write>
void write_region_set(int descriptor,const std::vector<Region>& regions,Value& state,bool restore,Checkpoint checkpoint,Write write) {
    const auto observations=region_inspect(descriptor,regions,state);
    std::vector<std::size_t> order;
    // The journal admits at most one torn old/new region. Repair that region
    // before advancing its durable marker to another region: a second restart
    // must never leave an earlier torn region outside the recorded allowance.
    if(restore)for(Json::ArrayIndex i=0;i<observations.size();++i)
        if(observations[i]["before"]==false && observations[i]["after"]==false)order.push_back(i);
    require(order.size()<=1,"foreign-gpt-change","More than one interrupted GPT region is not attributable to this writer");
    for(std::size_t i=0;i<regions.size();++i)if(order.empty() || order.front()!=i)order.push_back(i);
    for(const auto index:order) {
        const auto& region=regions[index]; const auto& next=restore ? region.before : region.after;
        if(storage_read(descriptor,region.offset,next.size())==next)continue;
        state["region"]=region.name; checkpoint(state,restore ? "GPT_RESTORE" : "GPT_WRITE");
        write(descriptor,region.offset,next); sync_fd(descriptor);
        require(storage_read(descriptor,region.offset,next.size())==next,"dualboot-gpt-readback","GPT readback differs after synchronization");
    }
}
void binding(const StorageTarget& target,const Value& plan,bool same_boot) {
    for(const auto* key:{"kind","partition","bytes","logical_sector_bytes","unit_identity_sha256","unit_identity_available","lun_address","device_family"})
        require(json(target.identity[key])==json(plan["target_identity"][key]),"wrong-dualboot-device","Device, unit, LUN or capacity differs from the journal");
    if(same_boot)require(target.identity["boot_id_sha256"]==plan["target_identity"]["boot_id_sha256"],"dualboot-reboot-required","Resume requires the same boot's original encrypted userdata mapping");
}
void original_gpt(int fd,const Value& plan) {
    for(const auto& row:plan["gpt"]["before"])require(sha256(storage_read(fd,row["offset"].asUInt64(),row["bytes"].asUInt()))==row["sha256"].asString(),"stale-dualboot-plan","GPT changed after preview");
}
void write_regions(const Root& system,const StorageTarget& target,const Value& plan,const Root& store,Value& state,bool restore,Quarantine* pending,const RecoveryBcb& recovery) {
    require(pending!=nullptr,"dualboot-quarantine-error","Every GPT effect requires the retained quarantine and runtime lease");
    pending->verify(); recovery.verify(system);
    const auto regions=load_regions(store,plan); (void)region_inspect(target.descriptor.get(),regions,state); protected_verify(target.descriptor.get(),plan,state["protected"]);
    auto descriptor=node(system,target.identity,true); // The caller retains the single canonical-LUN flock across this scoped write descriptor.
    write_region_set(descriptor.get(),regions,state,restore,[&](Value& current,const char* name) {
        phase(store,current,name,pending); pending->verify(); recovery.verify(system);
    },write_exact);
    state.removeMember("region"); protected_verify(target.descriptor.get(),plan,state["protected"]); recovery.verify(system);
    phase(store,state,restore ? "GPT_RESTORED_REBOOT_REQUIRED" : "COMMITTED_REBOOT_REQUIRED",pending);
}
std::string format_tool(const Value& row) {
    const auto type=row["filesystem"].asString(); if(type=="fat32")return "mkfs.fat"; if(type=="ext4")return "mke2fs"; if(type=="btrfs")return "mkfs.btrfs";
    if(type=="ntfs")return "mkfs.ntfs";
    require(type=="f2fs","unsupported-live-filesystem","Unknown live filesystem"); return tool_available("make_f2fs") ? "make_f2fs" : "mkfs.f2fs";
}
void formatter_admission(const Value& plan) {
    const auto& rows=plan["gpt"]["layout"]["rows"];
    require(rows.isArray() && rows.size()>=4 && rows.size()<=5,"invalid-dualboot-plan","Four or five bounded formatter roles are required");
    for(const auto& row:rows) {
        formatter_row(row); if(row["enabled"]==false)continue;
        const auto tool=format_tool(row); require(tool_available(tool),"dualboot-tool-unavailable","The selected bounded filesystem formatter is not packaged");
        const auto type=row["filesystem"].asString(); const auto checker=type=="fat32" ? "fsck.fat" : type=="ext4" ? "e2fsck" : type=="btrfs" ? "btrfs" : type=="f2fs" ? "fsck.f2fs" :
#ifdef __ANDROID__
            "fsck.ntfs";
#else
            "ntfsfix";
#endif
        require(tool_available(checker),"dualboot-tool-unavailable","The selected filesystem's read-only checker is not packaged");
    }
}
std::vector<std::string> format_arguments(const Value& row,const std::string& path) {
    const auto role=row["role"].asString(),type=row["filesystem"].asString();
    // The pinned formatter's automatic cluster sizing assumes 512-byte
    // sectors. One native 4 KiB sector per cluster keeps the minimum live
    // ESP above FAT32's data-cluster floor; readback still verifies its BPB.
    if(type=="fat32")return {"-F","32","-s","1","-n","ESP",path};
    if(type=="ext4")return {"-q","-F","-t","ext4","-L",role,path};
    if(type=="ntfs")return {"-Q","-L","Windows",path};
    if(type=="btrfs")return {"-f","-L",role,path};
    require(type=="f2fs","unsupported-live-filesystem","Unknown formatter");
    if(role=="userdata")return {"-f","-g","android","-t","0","-w","4096","-l","userdata",path};
    return {"-f","-l",role,path};
}
Value map_node(const DmTable& table,std::uint64_t bytes) {
    Value object; object["kernel_name"]="dm-"+std::to_string(minor(table.device)); object["device_number"]=devtext(table.device); object["bytes"]=Json::UInt64(bytes); return object;
}
void owned_view_policy(const DmTable& current,const DmTable& expected,int open_count) {
    require((open_count==1 || open_count==2) && current.name==expected.name && current.uuid==expected.uuid &&
        current.device==expected.device && current.type=="linear" && current.type==expected.type && current.start==expected.start &&
        current.length==expected.length && current.parameters==expected.parameters && current.event==expected.event &&
        current.open_count==open_count && current.flags==expected.flags && (current.flags&DM_ACTIVE_PRESENT_FLAG)!=0 &&
        (current.flags&(DM_READONLY_FLAG|DM_SUSPEND_FLAG|DM_INACTIVE_PRESENT_FLAG))==0,
        "stale-dualboot-view","The exact owned bounded mapper changed or another opener appeared during tool handoff");
}
template<class Effect,class Readback>
void owned_view_tool(const Root& system,const DmControl& control,const DmTable& view,std::uint64_t bytes,bool write,
    Quarantine& pending,const RecoveryBcb& recovery,Effect&& effect,Readback&& readback) {
    const auto object=map_node(view,bytes);
    dualboot_view_handoff(
        [&](bool exclusive){return node(system,object,write,exclusive,true);},
        [&](const Fd& descriptor,int count){
            struct stat st{};
            const auto flags=::fcntl(descriptor.get(),F_GETFL); int sector=0;
            require(flags>=0 && (flags&O_ACCMODE)==(write ? O_RDWR : O_RDONLY) &&
                ::fstat(descriptor.get(),&st)==0 && S_ISBLK(st.st_mode) && st.st_rdev==view.device && storage_bytes(descriptor.get())==bytes &&
                ::ioctl(descriptor.get(),BLKSSZGET,&sector)==0 && sector==4096,
                "stale-dualboot-view","Retained formatter descriptor no longer identifies the bounded mapper");
            owned_view_policy(control.table(view.name),view,count);
            pending.verify(); recovery.verify(system);
        },std::forward<Effect>(effect),std::forward<Readback>(readback));
}
void format_partitions(const Root& system,const StorageTarget& target,const Value& plan,const Value& userdata,const DmTable& crypto,
    DmControl& control,const Root& store,Value& state,Quarantine* pending,const RecoveryBcb& recovery) {
    require(pending!=nullptr,"dualboot-quarantine-error","Every filesystem effect requires the retained quarantine and runtime lease");
    const auto token=plan["plan_sha256"].asString().substr(0,24); const auto pool=plan["gpt"]["layout"]["pool"]["offset"].asUInt64();
    for(const auto& row:plan["gpt"]["layout"]["rows"])if(row["enabled"]==true) {
        const auto role=row["role"].asString(),name="ure-db-"+token+"-"+role,uuid_value="URE-DUALBOOT-"+plan["plan_sha256"].asString()+"-"+role;
        state["irreversible_data_loss"]=true; state["role"]=role; state["mapper_name"]=name; state["mapper_uuid"]=uuid_value; phase(store,state,"ERASE_AND_FORMAT",pending);
        const auto offset=role=="userdata" ? 0 : row["offset"].asUInt64()-pool; const auto backing=role=="userdata" ? crypto.device : devnumber(userdata["device_number"].asString());
        auto view=control.create_linear(name,uuid_value,backing,offset,row["bytes"].asUInt64());
        try {
            owned_view_tool(system,control,view,row["bytes"].asUInt64(),true,*pending,recovery,[&](const Fd& descriptor){
              const auto result=run_tool(format_tool(row),format_arguments(row,"/proc/self/fd/"+std::to_string(descriptor.get())),300,{}, {descriptor.get()});
              require(result.status==0 && !result.timed_out,"dualboot-format-failed","The bounded formatter failed; userdata may already be erased. Inspect the durable journal.");
            },[&](const Fd& descriptor){
              sync_fd(descriptor.get()); const auto signature=filesystem_probe(descriptor.get());
              require(signature["type"]==(row["filesystem"]=="fat32" ? "vfat" : row["filesystem"].asString()) &&
                  (role!="userdata" || signature["filesystem_encryption_feature"]==true),"dualboot-filesystem-readback","The bounded formatted filesystem or userdata encryption feature differs");
              if(role=="esp")esp_bpb_policy(storage_read(descriptor.get(),0,512),row["bytes"].asUInt64());
            });
            owned_view_tool(system,control,view,row["bytes"].asUInt64(),false,*pending,recovery,[&](const Fd& descriptor){
              require(filesystem_check(descriptor.get())["successful"]==true,"dualboot-filesystem-check","The newly formatted filesystem failed its independent read-only checker");
            },[](const Fd&){});
            // Retain the shortened owned userdata view until reboot, together
            // with the cooperating recovery lifecycle quarantine. Exact target
            // kernel claim/mount exclusion still requires physical acceptance;
            // a privileged raw shell is outside these cooperating guarantees.
            // No key is exported, persisted or used to reload the parent.
            if(role!="userdata")control.remove(name,uuid_value);
            else { state["userdata_guard"]["name"]=name; state["userdata_guard"]["uuid"]=uuid_value; state["userdata_guard"]["bytes"]=row["bytes"]; }
            if(!state["formatted_roles"].isArray())state["formatted_roles"]=Value(Json::arrayValue);
            state["formatted_roles"].append(role);
            protected_verify(target.descriptor.get(),plan,state["protected"]); phase(store,state,"FILESYSTEM_CHECKED",pending);
        } catch(...) {
            // Intentionally retain an operation-bound mapper after a format
            // attempt. Destroying it here could expose a partially erased or
            // wrongly sized userdata parent to the recovery GUI.
            phase(store,state,"FORMAT_INTERRUPTED_INSPECTION_REQUIRED",pending); throw;
        }
    }
    state.removeMember("role"); state.removeMember("mapper_name"); state.removeMember("mapper_uuid"); phase(store,state,"FILESYSTEMS_VERIFIED",pending);
}
Value device_result(const Value& state) {
    Value result; result["schema"]=1; result["operation"]="dualboot.setup"; result["state"]=state["phase"]; result["plan_sha256"]=state["plan_sha256"];
    result["data_loss"]=state.get("irreversible_data_loss",false); result["userdata_data_rollback_available"]=false;
    result["metadata_written"]=false; result["other_partition_payloads_written"]=false; result["reboot_required"]=true; result["partition_nodes_refreshed"]=false;
    result["private_record"]=true; result["physical_test_record"]=false; result["cooperating_locks_only"]=true;
    result["retained_userdata_guard"]=state["userdata_guard"]; result["warning"]="Do not mount or reuse the old partition nodes. Reboot recovery before installing either OS. GPT restoration cannot restore erased userdata."; return result;
}
}

Value dualboot_device_commands(const Value& plan) {
    // Preview construction calls this before sealing the outer plan. This is
    // pure row/tool admission: no block, mapper, mount or journal is accessed.
    formatter_admission(plan); const auto& rows=plan["gpt"]["layout"]["rows"];
    Value commands(Json::arrayValue); std::set<std::string> roles,enabled;
    const std::array<std::string,5> order{"userdata","esp","linux_boot","linux","windows"}; unsigned previous=0;
    for(const auto& row:rows) {
        const auto role=row["role"].asString(); const auto found=std::find(order.begin(),order.end(),role);
        require(found!=order.end() && roles.insert(role).second,"invalid-dualboot-plan","Duplicated or unknown bounded formatter role");
        const auto position=static_cast<unsigned>(found-order.begin());
        require(roles.size()==1 ? position==0 : position>previous,"invalid-dualboot-plan","Bounded formatter roles are out of order"); previous=position;
        if(row["enabled"]==false)continue;
        enabled.insert(role);
        Value command; command["operation"]="erase-and-format-owned-device-mapper-view"; command["role"]=role;
        command["filesystem"]=row["filesystem"]; command["bytes"]=row["bytes"]; command["writes"]=true;
        command["tool"]=format_tool(row); command["argv"]=Value(Json::arrayValue);
        for(const auto& argument:format_arguments(row,"<owned-bounded-view-fd>"))command["argv"].append(argument);
        command["scope"]="original-userdata-only";
        command["destination_placeholder"]="An inherited descriptor for the exact owned DM-linear view; never the whole LUN or unbounded userdata";
        command["view_backing"]=role=="userdata" ? "existing-verified-metadata-encryption-map" : "original-userdata-raw-partition";
        command["view_sector_start"]=Json::UInt64(0); command["view_length_512_sectors"]=Json::UInt64(row["bytes"].asUInt64()/512);
        if(role=="userdata")command["view_backing_offset_512_sectors"]=Json::UInt64(0);
        else {
            require(row["offset"].isUInt64() && plan["gpt"]["layout"]["pool"]["offset"].isUInt64() &&
                row["offset"].asUInt64()>=plan["gpt"]["layout"]["pool"]["offset"].asUInt64(),"invalid-dualboot-plan","Bounded formatter view precedes userdata");
            command["view_backing_offset_512_sectors"]=Json::UInt64((row["offset"].asUInt64()-plan["gpt"]["layout"]["pool"]["offset"].asUInt64())/512);
        }
        commands.append(command);
    }
    require(roles.contains("esp") && roles.contains("linux") && roles.contains("windows"),"invalid-dualboot-plan","Missing bounded formatter allocation roles");
    require(enabled.contains("userdata") && enabled.contains("esp") && (enabled.contains("linux") || enabled.contains("windows")) &&
        (!enabled.contains("linux_boot") || enabled.contains("linux")),"invalid-dualboot-plan","Userdata, ESP and an OS are required; separate boot requires Linux");
    return commands;
}

Value dualboot_device_preflight(const Root& system,const StorageTarget& target,const Value& plan) {
    Value out; out["schema"]=1; out["format"]="ure-dualboot-device-preflight"; out["checks"]=Value(Json::arrayValue); out["blockers"]=Value(Json::arrayValue);
    out["read_only"]=true; out["physical_test_record"]=false; out["private_record"]=true; out["legacy_live_writer_enabled"]=false;
    auto check=[&](const char* name,const auto& fn) { Value item; item["check"]=name; try { fn(); item["passed"]=true; }
        catch(const Error& error) { item["passed"]=false; item["error_code"]=error.code; item["reason"]=error.what(); out["blockers"].append(name); }
        catch(const std::exception&) { item["passed"]=false; item["error_code"]="invalid-dualboot-observation"; item["reason"]="Malformed or unavailable admission observation"; out["blockers"].append(name); }
        out["checks"].append(item); };
    check("sealed-userdata-only-recreate-plan",[&]{device_plan(plan); (void)untouched_ranges(plan);});
    check("reviewed-bounded-formatter-command-array",[&]{
        require(json(plan["device_commands"])==json(dualboot_device_commands(plan)),"stale-dualboot-commands","The exact bounded formatter command array differs from the reviewed preview");});
    check("current-root-uke-unlocked-identity",[&]{current_root(system);});
    check("existing-persistent-recovery-bcb",[&]{current_root(system); (void)RecoveryBcb(system,plan);});
    check("no-pending-dualboot-quarantine",[&]{current_root(system); struct stat st{}; errno=0;
        require(::fstatat(system.fd(),"tmp/uke-dualboot",&st,AT_SYMLINK_NOFOLLOW)!=0 && errno==ENOENT,"dualboot-quarantine-active","A prior dualboot intent remains pending; inspect its USB journal and reboot recovery after a verified terminal state");});
    check("retained-lun-and-original-gpt",[&]{storage_revalidate(target,&system); binding(target,plan,true); original_gpt(target.descriptor.get(),plan);});
    check("metadata-readonly-norecovery",[&]{current_root(system); (void)mounted_metadata(system,plan);});
    check("slot-and-virtual-ab-exclusion",[&]{current_root(system); check_snapshot(system,plan);});
    check("all-visible-mount-process-swap-usb-owners-idle",[&]{current_root(system); device_usage(system,plan);});
    check("direct-idle-metadata-encryption-map",[&]{current_root(system); const auto graph=storage_graph(system),userdata=original_userdata(graph,plan),object=encryption_object(graph,userdata);
        DmControl control(system); auto table=control.table(object["mapper_name"].asString()); require(table.device==devnumber(object["device_number"].asString()),"stale-dualboot-device","Mapper and sysfs devices differ");
        (void)crypto_policy(table,userdata); auto fd=node(system,object,false); require(filesystem_probe(fd.get())["type"]=="f2fs","unsupported-userdata-policy","The decrypted installed userdata filesystem is not F2FS");});
    check("installed-wrapped-f2fs-policy",[&]{current_root(system); crypto_fstab(system);});
    check("packaged-formatters-and-readonly-checkers",[&]{device_plan(plan); formatter_admission(plan);});
    out["eligible"]=out["blockers"].empty(); out["journal_checked_at_execute"]=true; out["atomic_snapshot"]=false;
    out["scope"]="Explicit F2FS userdata erase; native DM-linear views; no metadata, firmware or other partition payload writes. Read-only preflight cannot acquire future DM view claims.";
    return out;
}

Value dualboot_device_execute(const Root& system,const Value& plan,const fs::path& journal,const std::string& confirmation,const std::string& policy) {
    device_plan(plan);
    require(json(plan["device_commands"])==json(dualboot_device_commands(plan)),"stale-dualboot-commands","The exact bounded formatter command array differs from the reviewed preview");
    require(confirmation==plan["plan_sha256"].asString() && policy=="ERASE USERDATA","confirmation-required","Confirm this exact dualboot plan hash and ERASE USERDATA"); current_root(system);
    auto lease=RuntimeActivityLease::acquire(nullptr,true);
    require(lease.valid(),"dualboot-runtime-busy","A cooperating recovery operation or lifecycle transition prevents dualboot setup");
    auto target=storage_select(system,plan["target_identity"]["stable_id"].asString());
    const auto preflight=dualboot_device_preflight(system,target,plan); require(preflight["eligible"]==true,"dualboot-preflight-blocked","One or more explicit device preflight conditions failed; inspect dualboot preflight before applying");
    RecoveryBcb recovery(system,plan);
    require(::flock(target.descriptor.get(),LOCK_EX|LOCK_NB)==0,"dualboot-busy","A cooperating dualboot writer already owns the measured LUN");
    auto store=private_directory(journal,true); journal_usb(system,store); auto lock=store.open("owner.lock",O_RDWR|O_CREAT|O_EXCL,0600);
    require(::flock(lock.get(),LOCK_EX|LOCK_NB)==0,"dualboot-busy","The external journal is already owned");
    const auto graph=storage_graph(system),userdata=original_userdata(graph,plan),object=encryption_object(graph,userdata); DmControl control(system); auto crypto=control.table(object["mapper_name"].asString()); (void)crypto_policy(crypto,userdata);
    Value source; const auto regions=gpt_layout_regions(target,plan["gpt"]["layout"]["request"],plan["firmware_profile"].asString(),source,&system);
    require(source["layout_sha256"]==plan["gpt"]["layout"]["layout_sha256"],"stale-dualboot-plan","Regenerated layout differs from the preview");
    store.save_record("plan.json",plan); Value state; state["schema"]=1; state["format"]="ure-live-dualboot-journal"; state["plan_sha256"]=plan["plan_sha256"];
    state["irreversible_data_loss"]=false; state["userdata_data_backup_available"]=false; state["protected"]=protected_digests(target.descriptor.get(),plan);
    state["crypto_identity"]["name"]=crypto.name; state["crypto_identity"]["uuid"]=crypto.uuid; state["crypto_identity"]["device_number"]=devtext(crypto.device); state["crypto_identity"]["length_sectors"]=Json::UInt64(crypto.length);
    for(const auto& range:regions) {
        Value expected; for(const auto& row:plan["gpt"]["after"])if(row["name"]==range.name)expected=row;
        require(expected["offset"].asUInt64()==range.offset && expected["bytes"].asUInt64()==range.bytes.size() && expected["sha256"]==sha256(range.bytes),"stale-dualboot-plan","Generated GPT bytes differ from the reviewed plan");
        auto before=store.open("before-"+range.name+".bin",O_WRONLY|O_CREAT|O_EXCL,0600),after=store.open("after-"+range.name+".bin",O_WRONLY|O_CREAT|O_EXCL,0600);
        write_exact(before.get(),0,storage_read(target.descriptor.get(),range.offset,range.bytes.size())); write_exact(after.get(),0,range.bytes); sync_fd(before.get()); sync_fd(after.get());
    }
    sync_fd(store.fd()); phase(store,state,"BEFORE_GPT_VERIFIED"); original_gpt(target.descriptor.get(),plan); (void)load_regions(store,plan);
    // Last complete ownership/OTA pass occurs before creating any raw writer.
    require(dualboot_device_preflight(system,target,plan)["eligible"]==true,"dualboot-preflight-changed","Device ownership or Android state changed during journal preparation");
    Quarantine pending(system,target,plan,store,journal,true,lease); pending.checkpoint(state);
    format_partitions(system,target,plan,userdata,crypto,control,store,state,&pending,recovery);
    pending.verify(); check_snapshot(system,plan); device_usage(system,plan);
    original_gpt(target.descriptor.get(),plan); write_regions(system,target,plan,store,state,false,&pending,recovery); return device_result(state);
}

Value dualboot_device_recover(const Root& system,const fs::path& journal,const std::string& action,const std::string& confirmation,const std::string& policy) {
    require(action=="inspect" || action=="restore-gpt","unsupported-dualboot-recovery","Available recovery actions are inspect and restore-gpt; formats are never silently replayed");
    current_root(system); auto store=private_directory(journal,false); journal_usb(system,store); const auto plan=read_record(store,"plan.json"); device_plan(plan); auto state=read_record(store,"state.json");
    require(state["schema"]==1 && state["format"]=="ure-live-dualboot-journal" && state["plan_sha256"]==plan["plan_sha256"],"invalid-dualboot-journal","Journal identity differs from the sealed preview");
    auto lock=store.open("owner.lock",O_RDWR); require(::flock(lock.get(),LOCK_EX|LOCK_NB)==0,"dualboot-busy","The journal still has an active owner");
    auto target=storage_select(system,plan["target_identity"]["stable_id"].asString()); binding(target,plan,false);
    require(::flock(target.descriptor.get(),LOCK_EX|LOCK_NB)==0,"dualboot-busy","A cooperating operation owns this LUN");
    const auto regions=load_regions(store,plan); protected_verify(target.descriptor.get(),plan,state["protected"]); auto observations=region_inspect(target.descriptor.get(),regions,state);
    if(action=="restore-gpt") {
        require(confirmation==plan["plan_sha256"].asString() && policy=="RESTORE GPT ONLY; USERDATA REMAINS ERASED","confirmation-required","Explicitly confirm that GPT restoration cannot recover erased userdata");
        auto lease=RuntimeActivityLease::acquire(nullptr,true);
        require(lease.valid(),"dualboot-runtime-busy","A cooperating recovery operation or lifecycle transition prevents GPT restoration");
        check_snapshot(system,plan); device_usage(system,plan);
        RecoveryBcb recovery(system,plan);
        struct stat existing{}; const bool absent=::fstatat(system.fd(),"tmp/uke-dualboot",&existing,AT_SYMLINK_NOFOLLOW)!=0 && errno==ENOENT;
        Quarantine pending(system,target,plan,store,journal,absent,lease); pending.checkpoint(state);
        write_regions(system,target,plan,store,state,true,&pending,recovery);
    }
    auto result=device_result(state); result["read_only"]=action=="inspect"; result["gpt_regions"]=observations; result["protected_payloads_verified"]=true;
    result["resume_supported"]=false; result["recovery_note"]="Filesystem formatting is intentionally not idempotently resumed. Inspect or restore GPT, reboot recovery, reopen the installed encryption map and create a fresh explicitly destructive plan."; return result;
}
} // namespace ure
