// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <linux/fs.h>
#include <map>
#include <set>
#include <sstream>
#include <sys/ioctl.h>
#include <unistd.h>

namespace ure {
static std::uint64_t integer(const std::string& text) {
    require(!text.empty() && text.size() <= 32, "invalid-number", "Invalid storage number");
    std::size_t consumed = 0;
    std::uint64_t result = 0;
    try { result = std::stoull(text, &consumed); } catch (...) { throw Error("invalid-number", "Invalid storage number"); }
    require(text.front() >= '0' && text.front() <= '9' && text.find_first_not_of(" \r\n", consumed) == std::string::npos,
        "invalid-number", "Invalid storage number suffix");
    return result;
}
static std::string optional_read(const Root& system, const std::string& path) {
    try { return system.read(path, 65536); } catch (const Error&) { return {}; }
}
Value storage_graph(const Root& system) {
    Value result; result["objects"] = Value(Json::arrayValue);
    result["warnings"] = Value(Json::arrayValue); result["read_only"] = true;
    const std::string base = "sys/class/block";
    if (!system.exists(base)) { result["warnings"].append("Block inventory is unavailable"); return result; }
    std::set<std::string> seen;
    result["edges"]=Value(Json::arrayValue);
    std::vector<std::string> mount_rows;
    result["mounts_available"]=false;
    try {
        // Numeric PID avoids following proc/self, which is a procfs magic link.
        // The old directory spelling remains only for synthetic fixture trees.
        std::string path="proc/"+std::to_string(::getpid())+"/mountinfo";
        if(!system.exists(path))path="proc/self/mountinfo";
        std::istringstream table(system.read(path,4*1024*1024)); std::string row;
        while(std::getline(table,row))mount_rows.push_back(row);
        result["mounts_available"]=true;
    } catch(const Error& error) { if(error.code!="path-unavailable")throw; }
    for (const auto& name : system.list(base)) {
        require(identifier(name), "invalid-device", "Unexpected sysfs device name");
        std::string object_path = base + "/" + name;
        // Real sysfs class entries are links. Resolve only their kernel-owned
        // target beneath sys/devices, never an arbitrary filesystem symlink.
        try {
            const auto link = system.link(object_path);
            const auto resolved = (fs::path(base) / link).lexically_normal().generic_string();
            require(resolved.starts_with("sys/devices/"), "invalid-sysfs", "Block link escapes sys/devices");
            object_path = resolved;
        } catch (const Error& e) { if (e.code != "path-unavailable") throw; }
        const auto uevent = optional_read(system, object_path + "/uevent");
        Value item; item["kernel_name"] = name; item["sysfs_path"] = object_path;
        std::istringstream input(uevent); std::string line;
        std::map<std::string, std::string> fields;
        while (std::getline(input, line)) {
            const auto equal = line.find('=');
            if (equal != line.npos)require(fields.emplace(line.substr(0,equal),line.substr(equal+1)).second,"invalid-sysfs","Duplicate sysfs property");
        }
        item["label"] = fields["PARTNAME"]; item["partuuid"] = fields["PARTUUID"];
        item["device_number"] = fields["MAJOR"] + ":" + fields["MINOR"];
        item["partition"] = system.exists(object_path + "/partition");
        if(item["partition"].asBool())item["partition_index"]=Json::UInt64(integer(system.read(object_path+"/partition",32)));
        const auto parent_path=item["partition"].asBool() ? fs::path(object_path).parent_path().generic_string() : object_path;
        item["parent_lun_sysfs"]=parent_path;
        item["parent_lun_name"]=fs::path(parent_path).filename().string();
        require(!fields["MAJOR"].empty() && !fields["MINOR"].empty(),"invalid-sysfs","Missing block device number");
        integer(fields["MAJOR"]); integer(fields["MINOR"]);
        if (!fields["PARTUUID"].empty()) {
            std::transform(fields["PARTUUID"].begin(),fields["PARTUUID"].end(),fields["PARTUUID"].begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            item["partuuid"] = fields["PARTUUID"];
            require(uuid(fields["PARTUUID"]) && seen.insert(fields["PARTUUID"]).second,
                "ambiguous-identity", "Invalid or duplicate PARTUUID");
            item["stable_id"] = "partuuid:" + fields["PARTUUID"];
        } else item["stable_id"] = "sysfs:" + object_path;
        const auto size = optional_read(system, object_path + "/size");
        if (!size.empty()) {
            const auto sectors = integer(size);
            require(sectors <= UINT64_MAX / 512, "overflow", "Sysfs size overflows byte capacity");
            item["bytes"] = Json::UInt64(sectors * 512);
        }
        const auto start = optional_read(system, object_path + "/start");
        if (!start.empty()) item["start_512_sectors"] = Json::UInt64(integer(start));
        const auto logical = optional_read(system, parent_path + "/queue/logical_block_size");
        if (!logical.empty()) item["logical_sector_bytes"] = Json::UInt64(integer(logical));
        const auto ro = optional_read(system, object_path + "/ro");
        item["read_only_state"] = ro.empty() ? Value() : Value(integer(ro) != 0);
        const std::string label = fields["PARTNAME"];
        if (label == "uke_linux") item["owner"] = "LINUX_ROOT";
        else if (label == "uke_esp") item["owner"] = "ESP_SHARED";
        else if (label == "uke_windows") item["owner"] = "WINDOWS_ROOT";
        else if (label.starts_with("recovery_")) item["owner"] = "RECOVERY";
        else if (label == "userdata" || label == "metadata") item["owner"] = "ANDROID_DATA";
        else if (label == "super" || label.starts_with("boot_") || label.starts_with("init_boot_") || label.starts_with("vendor_boot_")) item["owner"] = "ANDROID_SYSTEM";
        else item["owner"] = "UNKNOWN";
        item["write_policy"]=label=="uke_linux" || label=="uke_esp" || label=="uke_windows" ? "PLAN_REQUIRED" : "PROTECTED_OR_UNCLASSIFIED";
        if (label.ends_with("_a") || label.ends_with("_b")) item["slot"] = label.substr(label.size() - 1);
        item["mounts"] = Value(Json::arrayValue);
        for (const auto& table_line : mount_rows) {
            std::istringstream row(table_line); std::string id,parent,device,root,mount;
            if (row >> id >> parent >> device >> root >> mount && device == item["device_number"].asString()) {
                Value mounted; mounted["path"] = mount; item["mounts"].append(mounted);
            }
        }
        if(item["partition"].asBool()) { Value edge; edge["relation"]="partition-of"; edge["source"]=item["stable_id"]; edge["target"]="sysfs:"+parent_path; result["edges"].append(edge); }
        item["dependencies_available"]=system.exists(object_path+"/slaves") && system.exists(object_path+"/holders");
        for(const auto* relation:{"slaves","holders"}) {
            item[relation]=Value(Json::arrayValue);
            if(system.exists(object_path+"/"+relation))for(const auto& peer:system.list(object_path+"/"+relation,128)) {
                require(identifier(peer),"invalid-device","Invalid block dependency name"); item[relation].append(peer);
                if(std::string_view(relation)=="slaves") { Value edge; edge["relation"]="depends-on"; edge["source"]=item["stable_id"]; edge["target_kernel_name"]=peer; result["edges"].append(edge); }
            }
        }
        for(const auto* field:{"name","uuid"}) {
            const auto data=optional_read(system,object_path+"/dm/"+field);
            if(!data.empty())item[std::string("mapper_")+field]=data.substr(0,data.find_first_of("\r\n"));
        }
        result["objects"].append(item);
    }
    return result;
}
std::uint64_t storage_bytes(int fd) {
    struct stat st{};
    require(::fstat(fd,&st) == 0, "io-error", "Cannot inspect storage descriptor");
    if (S_ISREG(st.st_mode)) { require(st.st_size >= 0, "invalid-size", "Negative image size"); return static_cast<std::uint64_t>(st.st_size); }
    std::uint64_t bytes = 0;
    require(S_ISBLK(st.st_mode) && ::ioctl(fd,BLKGETSIZE64,&bytes) == 0, "invalid-storage", "Expected an image or readable block object");
    require(bytes<=INT64_MAX,"invalid-size","Storage exceeds supported signed offsets");
    return bytes;
}
static std::vector<unsigned char> read_at(int fd, std::uint64_t offset, std::size_t size) {
    require(size <= 4 * 1024 * 1024 && offset <= static_cast<std::uint64_t>(INT64_MAX) &&
        size<=static_cast<std::uint64_t>(INT64_MAX)-offset, "size-limit", "Storage read bounds exceeded");
    std::vector<unsigned char> data(size); std::size_t done = 0;
    while (done < size) {
        const auto n = ::pread(fd, data.data()+done, size-done, static_cast<off_t>(offset + done));
        if (n < 0 && errno == EINTR) continue;
        require(n > 0, "truncated-read", "Storage image was truncated or unreadable");
        done += static_cast<std::size_t>(n);
    }
    return data;
}
std::string storage_read(int fd, std::uint64_t offset, std::size_t bytes) {
    const auto data=read_at(fd,offset,bytes);
    return std::string(reinterpret_cast<const char*>(data.data()),data.size());
}
static std::uint32_t le32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
        static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}
static std::uint64_t le64(const unsigned char* p) { return le32(p) | static_cast<std::uint64_t>(le32(p+4)) << 32; }
static std::uint32_t crc32(const std::vector<unsigned char>& bytes) {
    std::uint32_t crc = UINT32_MAX;
    for (const auto byte : bytes) { crc ^= byte; for (unsigned i=0;i<8;++i) crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U); }
    return crc ^ UINT32_MAX;
}
static std::string guid(const unsigned char* p, bool gpt_order = true) {
    static constexpr char hex[]="0123456789abcdef";
    static constexpr std::array<unsigned,16> order{3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15};
    std::string value;
    for (unsigned i=0;i<16;++i) { if(i==4 || i==6 || i==8 || i==10) value+='-'; const auto b=p[gpt_order ? order[i] : i]; value+=hex[b>>4]; value+=hex[b&15]; }
    return value;
}
struct Header { Value data; std::vector<unsigned char> entries; std::uint64_t first=0,last=0; std::uint32_t entry_size=0,count=0; bool valid=false; };
static Header read_header(int fd, std::uint64_t lba, std::uint32_t sector, std::uint64_t sectors) {
    Header output;
    const auto data = read_at(fd,lba*sector,sector);
    require(std::memcmp(data.data(),"EFI PART",8)==0, "invalid-gpt", "GPT header signature is missing");
    const auto header_size=le32(data.data()+12);
    require(header_size==92 && header_size<=sector && le32(data.data()+8)==0x00010000 &&
        std::all_of(data.begin()+92,data.end(),[](unsigned char c){return c==0;}),
        "invalid-gpt", "Unsupported GPT header revision or size");
    auto header=std::vector<unsigned char>(data.begin(),data.begin()+header_size);
    const auto expected=le32(data.data()+16); std::fill(header.begin()+16,header.begin()+20,0);
    const auto current=le64(data.data()+24), alternate=le64(data.data()+32), table=le64(data.data()+72);
    output.first=le64(data.data()+40); output.last=le64(data.data()+48);
    output.count=le32(data.data()+80); output.entry_size=le32(data.data()+84);
    require(current==lba && alternate==(lba==1 ? sectors-1 : 1) && output.first>1 && output.first<=output.last && output.last<sectors-1 && le32(data.data()+20)==0,
        "invalid-gpt", "GPT geometry is outside the image");
    require(output.count>0 && output.count<=4096 && (output.entry_size==128 || output.entry_size==256 || output.entry_size==512),
        "invalid-gpt", "GPT entry bounds exceeded");
    const std::uint64_t bytes=static_cast<std::uint64_t>(output.count)*output.entry_size;
    require(table<sectors && bytes<=4*1024*1024 && bytes <= (sectors-table)*sector,
        "invalid-gpt", "GPT entry table is outside the image");
    const auto reserved=std::max<std::uint64_t>(16384,bytes);
    require(reserved <= (sectors-table)*sector,"invalid-gpt","Reserved GPT table area is outside storage");
    const auto table_end=table+(reserved+sector-1)/sector-1;
    require(table>1 && (lba==1 ? table_end<output.first : table>output.last && table_end<current) &&
        !(table<=current && current<=table_end) && !(table<=alternate && alternate<=table_end),
        "invalid-gpt","GPT metadata overlaps usable space or a header");
    output.entries=read_at(fd,table*sector,static_cast<std::size_t>(bytes));
    output.data["header_crc_valid"]=crc32(header)==expected;
    output.data["entries_crc_valid"]=crc32(output.entries)==le32(data.data()+88);
    output.data["disk_guid"]=guid(data.data()+56);
    require(uuid(output.data["disk_guid"].asString()),"invalid-gpt","GPT disk GUID is zero");
    output.data["current_lba"]=Json::UInt64(current); output.data["alternate_lba"]=Json::UInt64(alternate);
    output.data["first_usable_lba"]=Json::UInt64(output.first); output.data["last_usable_lba"]=Json::UInt64(output.last);
    output.data["table_lba"]=Json::UInt64(table); output.data["entry_count"]=output.count; output.data["entry_size"]=output.entry_size;
    output.valid=output.data["header_crc_valid"].asBool() && output.data["entries_crc_valid"].asBool();
    return output;
}
static std::pair<std::string,bool> partition_name(const unsigned char* entry) {
    std::string name;
    for(unsigned offset=56;offset<128;offset+=2) {
        std::uint32_t code=static_cast<unsigned>(entry[offset]) | static_cast<unsigned>(entry[offset+1])<<8;
        if(code==0)break;
        if(code>=0xd800 && code<=0xdbff) {
            if(offset+3>=128)return {name,false};
            const auto low=static_cast<unsigned>(entry[offset+2]) | static_cast<unsigned>(entry[offset+3])<<8;
            if(low<0xdc00 || low>0xdfff)return {name,false};
            code=0x10000+((code-0xd800)<<10)+(low-0xdc00); offset+=2;
        } else if(code>=0xdc00 && code<=0xdfff)return {name,false};
        if(code<0x80)name+=static_cast<char>(code);
        else if(code<0x800) { name+=static_cast<char>(0xc0|(code>>6)); name+=static_cast<char>(0x80|(code&63)); }
        else if(code<0x10000) { name+=static_cast<char>(0xe0|(code>>12)); name+=static_cast<char>(0x80|((code>>6)&63)); name+=static_cast<char>(0x80|(code&63)); }
        else { name+=static_cast<char>(0xf0|(code>>18)); name+=static_cast<char>(0x80|((code>>12)&63)); name+=static_cast<char>(0x80|((code>>6)&63)); name+=static_cast<char>(0x80|(code&63)); }
    }
    return {name,true};
}
static Value partition_layout(const Header& header, std::uint32_t sector) {
    Value output; output["valid"]=true; output["partitions"]=Value(Json::arrayValue); output["reserved_records"]=Value(Json::arrayValue);
    bool valid=true; std::vector<std::pair<std::uint64_t,std::uint64_t>> ranges; std::set<std::string> ids;
    for(std::uint32_t i=0;i<header.count;++i) {
        const auto* p=header.entries.data()+static_cast<std::size_t>(i)*header.entry_size;
        const bool unused=std::all_of(p,p+16,[](unsigned char c){return c==0;});
        if(unused && std::all_of(p,p+header.entry_size,[](unsigned char c){return c==0;}))continue;
        Value item; item["index"]=i+1; item["type_guid"]=guid(p); item["partuuid"]=guid(p+16);
        const auto first=le64(p+32),last=le64(p+40); const auto name=partition_name(p);
        item["start_lba"]=Json::UInt64(first); item["end_lba"]=Json::UInt64(last); item["attributes"]=Json::UInt64(le64(p+48));
        item["label"]=name.first; item["label_encoding_valid"]=name.second;
        item["aligned_1mib"]=first<=UINT64_MAX/sector && (first*sector)%(1024*1024)==0;
        const bool range=first>=header.first && last<=header.last && first<=last && uuid(item["partuuid"].asString()) &&
            ids.insert(item["partuuid"].asString()).second && name.second &&
            std::all_of(p+128,p+header.entry_size,[](unsigned char c){return c==0;});
        // Pinned Uke OEM tables carry one non-partition last_parti reservation:
        // zero type GUID, a nonzero unique GUID and vendor attribute bit 60.
        // Preserve and expose its range separately. Other nonzero unused
        // records remain invalid rather than silently becoming free space.
        const bool reserved=unused && sector==4096 && (header.count==32 || header.count==96) && name.first=="last_parti" &&
            name.second && le64(p+48)==(1ULL<<60) && last==header.last;
        item["range_valid"]=range; valid=valid && range && (!unused || reserved);
        if(range) { item["bytes"]=Json::UInt64((last-first+1)*sector); ranges.emplace_back(first,last); }
        if(reserved) { item["kind"]="OEM_ZERO_TYPE_RESERVED_RECORD"; item["is_partition"]=false; output["reserved_records"].append(item); }
        else output["partitions"].append(item);
    }
    std::sort(ranges.begin(),ranges.end());
    for(std::size_t i=1;i<ranges.size();++i)if(ranges[i].first<=ranges[i-1].second)valid=false;
    output["valid"]=valid; return output;
}
Value gpt_inspect(int fd, std::uint32_t sector) {
    require(sector==512 || sector==4096, "invalid-sector", "Sector size must be 512 or 4096");
    const auto bytes=storage_bytes(fd);
    require(bytes%sector==0 && bytes/sector>=6, "invalid-size", "Image geometry cannot hold GPT");
    const auto sectors=bytes/sector;
    Value output; output["bytes"]=Json::UInt64(bytes); output["sector_bytes"]=sector; output["read_only"]=true;
    const auto mbr=read_at(fd,0,512);
    unsigned protective_entries=0, other_entries=0;
    bool protective_geometry=true;
    for(unsigned i=0;i<4;++i) {
        const auto* p=mbr.data()+446+i*16;
        if(p[4]==0xee) {
            ++protective_entries;
            protective_geometry=protective_geometry && p[0]==0 && le32(p+8)==1 &&
                le32(p+12)==std::min<std::uint64_t>(sectors-1,UINT32_MAX);
        } else if(std::any_of(p,p+16,[](unsigned char b){return b!=0;}))++other_entries;
    }
    output["protective_mbr_valid"]=mbr[510]==0x55 && mbr[511]==0xaa && protective_entries==1 && other_entries==0 && protective_geometry;
    output["hybrid_mbr"]=other_entries!=0;
    Header primary,backup;
    try { primary=read_header(fd,1,sector,sectors); output["primary"]=primary.data; }
    catch(const Error& e) { output["primary"]["error"]=e.code; }
    try { backup=read_header(fd,sectors-1,sector,sectors); output["backup"]=backup.data; }
    catch(const Error& e) { output["backup"]["error"]=e.code; }
    const auto primary_layout=primary.valid ? partition_layout(primary,sector) : Value();
    const auto backup_layout=backup.valid ? partition_layout(backup,sector) : Value();
    primary.valid=primary.valid && primary_layout["valid"]==true; backup.valid=backup.valid && backup_layout["valid"]==true;
    output["primary"]["valid"]=primary.valid; output["backup"]["valid"]=backup.valid;
    output["healthy"]=false; output["partitions"]=Value(Json::arrayValue); output["reserved_records"]=Value(Json::arrayValue);
    if(!primary.valid && !backup.valid) { output["state"]="NO_VALID_GPT"; return output; }
    const auto& header=primary.valid ? primary : backup;
    output["disk_guid"]=header.data["disk_guid"];
    output["partitions"]=(primary.valid ? primary_layout : backup_layout)["partitions"];
    output["reserved_records"]=(primary.valid ? primary_layout : backup_layout)["reserved_records"]; output["layout_valid"]=true;
    const bool matching=primary.valid && backup.valid && primary.entries==backup.entries && primary.count==backup.count &&
        primary.entry_size==backup.entry_size && primary.data["disk_guid"]==backup.data["disk_guid"] && primary.first==backup.first && primary.last==backup.last;
    output["copies_match"]=matching; output["healthy"]=matching && output["protective_mbr_valid"].asBool();
    output["state"]=output["healthy"].asBool() ? "HEALTHY" : "INSPECTION_REQUIRED";
    output["read_only"]=true;
    return output;
}
std::vector<StorageRange> gpt_regions(int fd, std::uint32_t sector) {
    const auto info=gpt_inspect(fd,sector);
    require(info["disk_guid"].isString(),"invalid-gpt","At least one valid GPT copy is required for a metadata backup");
    const auto sectors=info["bytes"].asUInt64()/sector;
    const auto& good=info[info["primary"]["valid"].asBool() ? "primary" : "backup"];
    const auto bytes=std::max<std::uint64_t>(16384,good["entry_count"].asUInt64()*good["entry_size"].asUInt64());
    const auto padded=(bytes+sector-1)/sector*sector;
    std::vector<StorageRange> result;
    for(const auto* side:{"primary","backup"}) {
        const bool primary=std::string_view(side)=="primary"; const auto& copy=info[side];
        const auto table=copy["table_lba"].isUInt64() ? copy["table_lba"].asUInt64() : primary ? 2 : sectors-1-padded/sector;
        const auto count=copy["entry_count"].isUInt64() ? copy["entry_count"].asUInt64() : good["entry_count"].asUInt64();
        const auto entry_size=copy["entry_size"].isUInt64() ? copy["entry_size"].asUInt64() : good["entry_size"].asUInt64();
        const auto size=std::max<std::uint64_t>(16384,count*entry_size); const auto table_bytes=(size+sector-1)/sector*sector;
        require(table>1 && table_bytes<4*1024*1024 && table<=sectors-1 && table_bytes/sector<=sectors-1-table &&
            (primary ? table+table_bytes/sector<=good["first_usable_lba"].asUInt64() : table>good["last_usable_lba"].asUInt64()),
            "ambiguous-gpt","Cannot bound both metadata copies without overlapping usable space");
        result.push_back({std::string(side)+"_table",table*sector,storage_read(fd,table*sector,static_cast<std::size_t>(table_bytes))});
        result.push_back({std::string(side)+"_header",(primary ? 1 : sectors-1)*sector,storage_read(fd,(primary ? 1 : sectors-1)*sector,sector)});
    }
    result.push_back({"protective_mbr",0,storage_read(fd,0,sector)}); return result;
}
static void store_le(std::vector<unsigned char>& data, std::size_t offset, std::uint64_t value, unsigned count) {
    for(unsigned i=0;i<count;++i)data[offset+i]=static_cast<unsigned char>((value>>(8*i))&255);
}
std::vector<StorageRange> gpt_repair_regions(int fd, std::uint32_t sector) {
    const auto info=gpt_inspect(fd,sector);
    require(info["protective_mbr_valid"]==true,"ambiguous-gpt","Repair requires a valid protective-only MBR");
    const bool primary=info["primary"]["valid"].asBool(),backup=info["backup"]["valid"].asBool();
    require(primary!=backup,"ambiguous-gpt","Repair requires exactly one valid GPT copy; two conflicting copies need manual inspection");
    const auto sectors=info["bytes"].asUInt64()/sector; const bool repair_primary=!primary;
    const auto good=read_header(fd,primary ? 1 : sectors-1,sector,sectors);
    auto header=read_at(fd,(primary ? 1 : sectors-1)*sector,sector);
    const auto reserved=std::max<std::uint64_t>(16384,good.entries.size()); const auto count=(reserved+sector-1)/sector;
    const std::uint64_t table=repair_primary ? 2 : sectors-1-count;
    require(repair_primary ? table+count<=good.first : table>good.last,"ambiguous-gpt","Canonical repair table would overlap usable space");
    store_le(header,24,repair_primary ? 1 : sectors-1,8); store_le(header,32,repair_primary ? sectors-1 : 1,8);
    store_le(header,72,table,8); store_le(header,16,0,4);
    store_le(header,16,crc32(std::vector<unsigned char>(header.begin(),header.begin()+92)),4);
    std::string entries(reinterpret_cast<const char*>(good.entries.data()),good.entries.size()); entries.resize(static_cast<std::size_t>(count*sector),'\0');
    const std::string side=repair_primary ? "primary" : "backup";
    return {{side+"_table",table*sector,std::move(entries)},
        {side+"_header",(repair_primary ? 1 : sectors-1)*sector,std::string(reinterpret_cast<const char*>(header.data()),header.size())}};
}
Value filesystem_probe_range(int fd,std::uint64_t offset,std::uint64_t bytes) {
    const auto capacity=storage_bytes(fd);
    require(offset<=capacity && bytes<=capacity-offset,"invalid-range","Filesystem signature range is outside selected storage");
    require(bytes>=512, "truncated-image", "Image is smaller than a sector");
    const auto head=read_at(fd,offset,static_cast<std::size_t>(std::min<std::uint64_t>(4096,bytes)));
    Value output; output["bytes"]=Json::UInt64(bytes); output["type"]="unknown"; output["encryption"]="none";
    if(head.size()>=8 && std::memcmp(head.data(),"LUKS\xba\xbe",6)==0) {
        output["type"]="encrypted"; output["encryption"]="LUKS";
        output["version"]=(static_cast<unsigned>(head[6])<<8)|head[7];
    } else if(std::memcmp(head.data()+3,"-FVE-FS-",8)==0) { output["type"]="encrypted"; output["encryption"]="BITLK"; }
    else if(std::memcmp(head.data()+3,"NTFS    ",8)==0)output["type"]="ntfs";
    else if(std::memcmp(head.data()+3,"EXFAT   ",8)==0)output["type"]="exfat";
    else if(std::memcmp(head.data()+82,"FAT32   ",8)==0 || std::memcmp(head.data()+54,"FAT16   ",8)==0)output["type"]="vfat";
    else if(head.size()>=1144 && head[1080]==0x53 && head[1081]==0xef) { output["type"]="ext4"; output["uuid"]=guid(head.data()+1128,false); }
    else if(head.size()>1028 && le32(head.data()+1024)==0xf2f52010U)output["type"]="f2fs";
    else if(std::memcmp(head.data(),"XFSB",4)==0)output["type"]="xfs";
    else if(head.size()>1028 && le32(head.data()+1024)==0xe0f5e1e2U)output["type"]="erofs";
    else if(std::memcmp(head.data(),"MSWIM",5)==0)output["type"]="wim";
    else if(bytes>=65536+4096) {
        const auto btrfs=read_at(fd,offset+65536,4096);
        if(std::memcmp(btrfs.data()+64,"_BHRfS_M",8)==0) { output["type"]="btrfs"; output["uuid"]=guid(btrfs.data()+32,false); output["generation"]=Json::UInt64(le64(btrfs.data()+72)); }
    }
    output["read_only"]=true; output["signature_only"]=true;
    return output;
}
Value filesystem_probe(int fd) { return filesystem_probe_range(fd,0,storage_bytes(fd)); }
} // namespace ure
