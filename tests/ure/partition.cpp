// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>

namespace {
void check(bool value,const std::string& message) { if(!value)throw std::runtime_error(message); }
template<class F> void reject(F action,const std::string& code) {
    try { action(); } catch(const ure::Error& error) { check(error.code==code,"Unexpected refusal "+error.code+", wanted "+code); return; }
    throw std::runtime_error("Expected refusal "+code);
}
void put(std::string& bytes,std::size_t offset,std::uint64_t value,unsigned count) {
    for(unsigned i=0;i<count;++i)bytes.at(offset+i)=static_cast<char>((value>>(8*i))&255);
}
std::uint32_t crc(std::string_view bytes) {
    std::uint32_t value=UINT32_MAX;
    for(const char c:bytes) { value^=static_cast<unsigned char>(c); for(unsigned i=0;i<8;++i)value=(value>>1)^((value&1U) ? 0xedb88320U : 0U); }
    return value^UINT32_MAX;
}
void entry(std::string& table,unsigned index,std::uint64_t first,std::uint64_t last,const std::string& name,bool reserved=false) {
    const auto offset=static_cast<std::size_t>(index)*128;
    if(!reserved)for(unsigned i=0;i<16;++i)table[offset+i]=static_cast<char>(i+11);
    for(unsigned i=0;i<16;++i)table[offset+16+i]=static_cast<char>(i+21);
    put(table,offset+16,index+1,4); put(table,offset+32,first,8); put(table,offset+40,last,8);
    if(reserved)put(table,offset+48,1ULL<<60,8);
    for(std::size_t i=0;i<name.size();++i)table[offset+56+i*2]=name[i];
}
std::string fixture(unsigned sector,bool many=false) {
    constexpr unsigned sectors=512;
    const unsigned count=many ? 256U : sector==4096 ? 32U : 128U;
    const auto table_bytes=std::max<unsigned>(16384,count*128),table_sectors=table_bytes/sector;
    const auto first=2+table_sectors,last=sectors-2-table_sectors,backup=last+1;
    std::string disk(sectors*sector,'\0'),table(table_bytes,'\0');
    if(many)for(unsigned i=0;i<132;++i)entry(table,i,first+i,first+i,"part"+std::to_string(i));
    else {
        entry(table,0,64,95,"uke_windows"); entry(table,1,160,319,"uke_linux"); entry(table,2,384,447,"userdata");
        if(sector==4096)entry(table,3,448,last,"last_parti",true);
        disk.replace(64*sector+3,8,"NTFS    ");
        disk.replace(160*sector+65536+64,8,"_BHRfS_M");
        disk.replace(384*sector,6,"LUKS\xba\xbe"); disk[384*sector+7]=2;
    }
    disk[450]=static_cast<char>(0xee); put(disk,454,1,4); put(disk,458,sectors-1,4); disk[510]=0x55; disk[511]=static_cast<char>(0xaa);
    disk.replace(2*sector,table.size(),table); disk.replace(backup*sector,table.size(),table);
    for(const auto lba:std::array<unsigned,2>{1,sectors-1}) {
        std::string header(sector,'\0'); header.replace(0,8,"EFI PART"); put(header,8,0x10000,4); put(header,12,92,4);
        put(header,24,lba,8); put(header,32,sectors-lba,8); put(header,40,first,8); put(header,48,last,8);
        for(unsigned i=0;i<16;++i)header[56+i]=static_cast<char>(i+91);
        put(header,72,lba==1 ? 2 : backup,8); put(header,80,count,4); put(header,84,128,4);
        put(header,88,crc(std::string_view(table).substr(0,count*128)),4); put(header,16,crc(std::string_view(header).substr(0,92)),4);
        disk.replace(lba*sector,sector,header);
    }
    return disk;
}
void save(const ure::fs::path& file,const std::string& data) {
    std::ofstream output(file,std::ios::binary); output.write(data.data(),static_cast<std::streamsize>(data.size())); check(output.good(),"Cannot write partition fixture");
}
}
int main(int argc,char* argv[]) {
    if(argc==3 && std::string_view(argv[1])=="--fixture") { save(argv[2],fixture(4096)); return 0; }
    std::array<char,40> pattern{}; const std::string text="/tmp/ure-partition-tests-XXXXXX"; std::copy(text.begin(),text.end(),pattern.begin());
    const auto* created=::mkdtemp(pattern.data()); if(!created)return 1; const ure::fs::path work(created);
    try {
        for(const unsigned sector:{512U,4096U}) {
            const auto file=work/(std::to_string(sector)+".img"); const auto pristine=fixture(sector); save(file,pristine);
            auto target=ure::storage_image(file,sector); const auto before=ure::sha256(target.descriptor.get());
            const auto map=ure::partition_map(target); const auto& parts=map["partitions"];
            check(map["gpt"]["healthy"]==true && parts.size()==3 && map["signature_samples"]==3U && map["management_eligible"]==false,"Partition map lost geometry or overstated ownership eligibility");
            check(parts[0]["content"]["type"]=="ntfs" && parts[1]["content"]["type"]=="btrfs" && parts[2]["content"]["encryption"]=="LUKS","Partition-offset signatures were not detected");
            check(parts[0]["owner_hint"]=="WINDOWS_ROOT" && parts[1]["ownership_verified"]==false && parts[2]["content"]["android_fbe_trust"]=="UNVERIFIED" && map["android_fbe_access_authorized"]==false,"Signature observation became installed-OS or Android trust proof");
            const std::uint64_t expected=sector==512 ? 96768 : 761856;
            check(map["unallocated_bytes"].asUInt64()==expected,"Gap accounting included allocated data or an OEM reservation");
            check(map["reserved_records"].size()==(sector==4096 ? 1U : 0U),"OEM reservation became a normal partition");
            if(sector==512)for(const auto& gap:map["unallocated_ranges"])check(gap["aligned_1mib_start_lba"].isNull(),"Alignment beyond a gap was advertised as available");
            check(ure::sha256(target.descriptor.get())==before && ure::json(ure::storage_image(file,sector).identity)==ure::json(target.identity),"Read-only partition mapping changed storage");
            reject([&]{ure::filesystem_probe_range(target.descriptor.get(),UINT64_MAX,512);},"invalid-range");
            reject([&]{ure::filesystem_probe_range(target.descriptor.get(),0,static_cast<std::uint64_t>(pristine.size())+1);},"invalid-range");
            reject([&]{ure::filesystem_probe_range(target.descriptor.get(),0,511);},"truncated-image");
            // A signature elsewhere must not leak through a short selected slice.
            check(ure::filesystem_probe_range(target.descriptor.get(),0,512)["type"]=="unknown","Filesystem slice escaped its range");
            auto damaged=pristine; damaged[sector+16]^=1; save(file,damaged); target=ure::storage_image(file,sector);
            const auto unavailable=ure::partition_map(target);
            check(unavailable["unallocated_ranges_available"]==false && unavailable["unallocated_bytes"].isNull() && unavailable["unallocated_ranges"].empty() &&
                unavailable["signature_samples"]==0U && unavailable["partitions"][0]["content"]["observation_state"]=="NOT_PROBED_UNHEALTHY_GPT","Unhealthy GPT produced free-space or signature guesses");
            save(file,fixture(sector,true)); target=ure::storage_image(file,sector);
            const auto bounded=ure::partition_map(target);
            check(bounded["gpt"]["healthy"]==true && bounded["partitions"].size()==132 && bounded["signature_samples"]==128U && bounded["signature_samples_truncated"]==true &&
                bounded["partitions"][128]["content"]["observation_state"]=="NOT_PROBED_LIMIT","Signature probe budget was exceeded or silently truncated");
            save(file,pristine); reject([&]{ure::partition_map(target);},"stale-device");
        }
        ure::fs::remove_all(work); std::cout<<"Partition map: 512/4096 gaps, OEM reservations, bounded partition-offset NTFS/Btrfs/LUKS probes, unknown ownership/trust, unhealthy GPT and stale identity passed; synthetic images only.\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; ure::fs::remove_all(work); return 1; }
}
