// SPDX-License-Identifier: Apache-2.0
// Host-only namespace fixture. No stock GPT, firmware, keys or userdata bytes.
#include <json/json.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr std::uint64_t sector=512,mib=1048576;
void check(bool ok,std::string_view why) { if(!ok)throw std::runtime_error(std::string(why)); }
void put(std::string& data,std::size_t offset,std::uint64_t value,unsigned count) {
    for(unsigned i=0;i<count;++i)data.at(offset+i)=static_cast<char>(value>>(8*i));
}
std::uint32_t crc(std::string_view data) {
    std::uint32_t value=UINT32_MAX;
    for(unsigned char byte:data) { value^=byte; for(unsigned i=0;i<8;++i)value=(value>>1)^((value&1) ? 0xedb88320U : 0U); }
    return value^UINT32_MAX;
}
void write(int fd,std::string_view data,std::uint64_t offset) {
    check(::pwrite(fd,data.data(),data.size(),static_cast<off_t>(offset))==static_cast<ssize_t>(data.size()),"Short fixture write");
}
void guid(std::string& data,std::size_t offset,unsigned disk,unsigned index) {
    for(unsigned i=0;i<16;++i)data.at(offset+i)=static_cast<char>(0x80+i);
    put(data,offset,disk,4); put(data,offset+4,index,4);
}
Json::Value load(const std::filesystem::path& file) {
    check(std::filesystem::is_regular_file(std::filesystem::symlink_status(file)) && std::filesystem::file_size(file)<65536,"Invalid fixture input");
    std::ifstream stream(file); Json::Value value; stream>>value; check(stream.good() || stream.eof(),"Cannot read fixture JSON"); return value;
}
void save(const std::filesystem::path& file,const Json::Value& value) {
    std::ofstream stream(file); check(stream.good(),"Cannot open fixture record"); stream<<value<<'\n'; check(stream.good(),"Cannot save fixture record");
}
}
int main(int argc,char** argv) {
    try {
        check(argc==3 || argc==4,"Pass the reviewed JSON, a fresh output directory and optional generated LP metadata");
        const auto input=load(argv[1]); const std::filesystem::path output=std::filesystem::canonical(argv[2]);
        check(std::filesystem::is_empty(output),"Output directory must be empty");
        check(input["schema_version"]==1 && input["fixture_kind"]=="stock-namespace-with-synthetic-physical-geometry" &&
            input["physical_geometry_observed"]==false && input["physical_gpt_captured"]==false && input["ufs_lu_numbers_known"]==false,
            "This generator accepts reviewed namespace evidence only");
        std::array<std::map<unsigned,std::string>,6> disks; std::set<std::string> labels;
        const std::array<unsigned,6> counts{32,6,6,3,67,7};
        for(const auto& item:input["physical_partition_labels"]) {
            const auto node=item["device"].asString(),label=item["name"].asString();
            check(node.starts_with("/dev/block/sd") && node.size()>=15 && node.at(13)>='a' && node.at(13)<='f',"Invalid observed disk name");
            const auto number=node.substr(14); check(number.find_first_not_of("0123456789")==number.npos,"Invalid partition index");
            const auto index=static_cast<unsigned>(std::stoul(number)); const unsigned disk=static_cast<unsigned>(node.at(13)-'a');
            check(index>0 && index<=counts[disk] && !label.empty() && label.size()<=36 && label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")==label.npos,
                "Invalid observed label or index");
            check(disks[disk].emplace(index,label).second && labels.insert(label).second,"Duplicate namespace entry");
        }
        check(labels.size()==121,"Incomplete 121-partition namespace");
        check(disks[0].at(31)=="super" && disks[0].at(32)=="userdata" && disks[4].at(67)=="dpm", "Observed terminal partition indices changed");
        const auto super_bytes=std::stoull(input["logical"]["super_device"]["total_size"].asString());
        check(super_bytes==11274289152ULL && input["lpdump_linear_extent_sector_bytes"]==512,"Unexpected super geometry");
        std::uint64_t previous=0,total=0; std::set<std::string> logical_names;
        for(const auto& range:input["logical_active_extents"]) {
            const auto begin=range["start_sector"].asUInt64(),count=range["sector_count"].asUInt64();
            check(count>0 && begin>=previous && begin%2048==0 && begin<=super_bytes/sector && count<=super_bytes/sector-begin &&
                range["end_sector_exclusive"].asUInt64()==begin+count && range["size_bytes"].asUInt64()==count*sector,"Invalid observed logical extent");
            check(logical_names.insert(range["name"].asString()).second,"Duplicate logical partition");
            previous=begin+count; total+=count*sector;
        }
        check(logical_names.size()==8 && total==7936512000ULL,"Incomplete observed logical allocation");
        Json::Value manifest; manifest["schema_version"]=1; manifest["physical_geometry"]= "synthetic-never-flash";
        manifest["source_record"]=input["source_record"]; manifest["partitions"]=Json::arrayValue;
        for(unsigned disk=0;disk<6;++disk) {
            check(disks[disk].size()==counts[disk],"Incomplete disk label group");
            std::string table(128*128,'\0'); std::uint64_t next=mib;
            for(const auto& [index,label]:disks[disk]) {
                // 1 MiB firmware placeholders and 512 MiB blank userdata are
                // intentionally unrelated to the tablet's physical capacities.
                const std::uint64_t bytes=label=="super" ? super_bytes : label=="userdata" ? 512*mib : mib;
                const auto offset=(index-1)*128; guid(table,offset,0x9999,1); guid(table,offset+16,disk+1,index);
                put(table,offset+32,next/sector,8); put(table,offset+40,(next+bytes)/sector-1,8);
                for(std::size_t i=0;i<label.size();++i)table.at(offset+56+i*2)=label[i];
                Json::Value row; row["label"]=label; row["observed_disk"]=std::string("sd")+static_cast<char>('a'+disk);
                row["partition_index"]=index; row["fixture_offset_bytes"]=Json::UInt64(next); row["fixture_bytes"]=Json::UInt64(bytes);
                row["physical_range_observed"]=false; manifest["partitions"].append(row); next+=bytes;
            }
            const auto bytes=next+mib,sectors=bytes/sector,last=sectors-34,backup=sectors-33;
            const auto file=output/(std::string("sd")+static_cast<char>('a'+disk)+".img");
            const int fd=::open(file.c_str(),O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW,0600); check(fd>=0,"Cannot create fresh fixture image");
            check(::ftruncate(fd,static_cast<off_t>(bytes))==0,"Cannot size sparse fixture"); write(fd,table,2*sector); write(fd,table,backup*sector);
            for(const auto lba:std::array<std::uint64_t,2>{1,sectors-1}) {
                std::string header(sector,'\0'); header.replace(0,8,"EFI PART"); put(header,8,0x10000,4); put(header,12,92,4);
                put(header,24,lba,8); put(header,32,sectors-lba,8); put(header,40,34,8); put(header,48,last,8);
                guid(header,56,disk+1,0); put(header,72,lba==1 ? 2 : backup,8); put(header,80,128,4); put(header,84,128,4); put(header,88,crc(table),4);
                put(header,16,crc(std::string_view(header).substr(0,92)),4); write(fd,header,lba*sector);
            }
            std::string mbr(sector,'\0'); mbr[450]=static_cast<char>(0xee); put(mbr,454,1,4); put(mbr,458,sectors-1,4);
            mbr[510]=0x55; mbr[511]=static_cast<char>(0xaa); write(fd,mbr,0); check(::fsync(fd)==0,"Cannot sync fixture GPT"); ::close(fd);
        }
        if(argc==4) {
            const std::filesystem::path metadata_file=argv[3];
            check(std::filesystem::is_regular_file(std::filesystem::symlink_status(metadata_file)) &&
                std::filesystem::file_size(metadata_file)>4096 && std::filesystem::file_size(metadata_file)<65536,"Invalid generated LP metadata file");
            std::ifstream stream(metadata_file,std::ios::binary); const std::string serialized((std::istreambuf_iterator<char>(stream)),{});
            // AOSP metadata-image files store one 4096-byte geometry followed
            // by one metadata copy. Populate the three primary/backup slots
            // at liblp's block-device offsets inside the synthetic super.
            const std::string_view geometry(serialized.data(),4096),metadata(serialized.data()+4096,serialized.size()-4096);
            check(static_cast<unsigned char>(geometry[0])==0x67 && static_cast<unsigned char>(metadata[0])==0x30,"Unexpected LP magic");
            const int fd=::open((output/"sda.img").c_str(),O_WRONLY|O_NOFOLLOW); check(fd>=0,"Cannot open synthetic super disk");
            const std::uint64_t base=31*mib; write(fd,geometry,base+4096); write(fd,geometry,base+8192);
            for(unsigned slot=0;slot<3;++slot) {
                write(fd,metadata,base+12288+slot*65536); write(fd,metadata,base+12288+(3+slot)*65536);
            }
            check(::fsync(fd)==0,"Cannot sync LP metadata"); ::close(fd);
            manifest["logical_metadata"]= "regenerated-from-observed-extents-not-stock-bytes";
        }
        save(output/"fixture.json",manifest);
        std::cout<<"Created six sparse namespace disks; every physical range and GUID is synthetic. Never flash these files.\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
