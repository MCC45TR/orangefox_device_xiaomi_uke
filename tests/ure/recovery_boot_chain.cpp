// SPDX-License-Identifier: Apache-2.0
// Branch excerpts are compiled from Qualcomm ABL aef4e86045bd151a54d33bab8ae5e8e62462a8f4.
// This source oracle does not establish that Xiaomi ships the same ABL binary.
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

constexpr unsigned BOOT_HEADER_VERSION_THREE=3, BOOT_HEADER_VERSION_FOUR=4;
constexpr unsigned IMG_RECOVERY=0, IMG_DTBO=1;
struct InfoState { bool MultiSlotBoot=true, BootIntoRecovery=true, HasBootInitRamdisk=true; unsigned HeaderVersion=4; };
struct HeaderState { unsigned header_version; };
static bool no_kernel=false;
static bool IsDynamicPartitionSupport() { return true; }
static bool IsBuildUseRecoveryAsBoot() { return false; }
static bool IsRecoveryHasNoKernel() { return no_kernel; }
static void AddRequestedPartition(std::vector<std::string>& values,unsigned image) {
    values.emplace_back(image==IMG_RECOVERY ? "recovery" : "dtbo");
}
#include "abl-branches.hpp"

static void require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
using Header=std::array<unsigned char,4096>;
extern "C" int abl_legacy_extent(const void*,std::uint32_t*);
static std::uint64_t u32(const Header& header,std::size_t at) {
    std::uint64_t result=0;
    for(unsigned i=0;i<4;++i)result|=static_cast<std::uint64_t>(header[at+i])<<(i*8);
    return result;
}
static std::uint64_t aligned(std::uint64_t value) { return (value+4095)/4096*4096; }
struct Result {
    unsigned version;
    std::uint64_t kernel,ramdisk,dtb=0,dtbo=0,payload=0,abl_extent=0;
    bool selects_recovery,substitutes_init_boot,requires_vendor_boot,vendor_boot_requested,structural_candidate=false;
};
static Result inspect(const Header& header,std::uint64_t bytes) {
    require(std::string(header.begin(),header.begin()+8)=="ANDROID!","Android boot magic is absent");
    const auto version=u32(header,40);
    require(version>=2 && version<=4,"Only recovery header versions 2, 3 and 4 are reviewed");
    Result result{}; result.version=static_cast<unsigned>(version);
    result.kernel=u32(header,8); result.ramdisk=u32(header,version<3 ? 16 : 12);
    require(result.ramdisk>0,"Recovery ramdisk is empty");
    no_kernel=result.kernel==0;
    InfoState info; info.HeaderVersion=result.version;
    result.selects_recovery=source_selects_recovery(&info,true);
    result.substitutes_init_boot=source_substitutes_init_boot(&info);
    const HeaderState boot_header{result.version};
    result.requires_vendor_boot=source_requires_vendor_boot(&boot_header);
    const auto requested=source_requested_partitions(&info);
    result.vendor_boot_requested=std::find(requested.begin(),requested.end(),"vendor_boot")!=requested.end();
    if(version==2) {
        result.dtbo=u32(header,1632); result.dtb=u32(header,1648);
        const auto dtbo_offset=u32(header,1636)+(u32(header,1640)<<32);
        result.payload=4096+aligned(result.kernel)+aligned(result.ramdisk)+aligned(result.dtbo)+aligned(result.dtb);
        std::uint32_t parser_extent=0;
        const auto parser_status=abl_legacy_extent(header.data(),&parser_extent);
        result.abl_extent=parser_extent;
        result.structural_candidate=result.kernel>0 && u32(header,36)==4096 && u32(header,1644)==1660 &&
            u32(header,24)==0 && result.dtbo>0 && result.dtb>0 &&
            dtbo_offset==4096+aligned(result.kernel)+aligned(result.ramdisk) &&
            parser_status==0 && result.abl_extent>=result.payload && result.abl_extent<=bytes && bytes<=104857600;
    }
    return result;
}
static void put(Header& header,std::size_t offset,std::uint32_t value) {
    for(unsigned i=0;i<4;++i)header[offset+i]=static_cast<unsigned char>(value>>(i*8));
}
static Header fixture(unsigned version,std::uint32_t kernel) {
    Header header{}; const std::string magic="ANDROID!";
    std::copy(magic.begin(),magic.end(),header.begin());
    put(header,40,version); put(header,8,kernel); put(header,version<3 ? 16 : 12,4096);
    if(version==2) {
        put(header,36,4096); put(header,1632,4096); put(header,1644,1660); put(header,1648,4096);
        put(header,1636,static_cast<std::uint32_t>(4096+aligned(kernel)+4096));
    }
    return header;
}
static void self_test() {
    auto result=inspect(fixture(4,0),104857600);
    require(!result.selects_recovery && !result.structural_candidate,"Kernel-less recovery was accepted as independent");
    result=inspect(fixture(4,4096),104857600);
    require(result.selects_recovery && result.substitutes_init_boot && !result.structural_candidate,
        "The actual v4 init_boot substitution was lost");
    result=inspect(fixture(3,4096),104857600);
    require(result.selects_recovery && !result.substitutes_init_boot && result.requires_vendor_boot &&
        !result.vendor_boot_requested && !result.structural_candidate,"The actual v3 missing vendor_boot dependency was lost");
    InfoState requested_state; requested_state.HeaderVersion=3;
    require(source_requested_partitions(&requested_state)==std::vector<std::string>{"recovery","dtbo","dtbo"},
        "The original kernel-bearing recovery request list differs");
    auto header=fixture(2,4096); result=inspect(header,24576);
    require(result.selects_recovery && !result.substitutes_init_boot && !result.requires_vendor_boot &&
        result.structural_candidate && result.payload==20480 && result.abl_extent==24576,
        "The v2 structural candidate or ABL double-DTB extent differs");
    require(!inspect(header,20480).structural_candidate,"Truncated ABL extent passed");
    for(const auto offset:{8U,36U,1632U,1636U,1644U,1648U}) {
        auto changed=header; put(changed,offset,0);
        require(!inspect(changed,104857600).structural_candidate,"Incomplete v2 structure passed");
    }
    for(const auto offset:{8U,16U,1632U,1636U,1640U,1648U}) {
        auto changed=header; put(changed,offset,UINT32_MAX);
        require(!inspect(changed,104857600).structural_candidate,"Overflowing v2 extent passed");
    }
    require(!inspect(header,104857601).structural_candidate,"Oversized recovery passed");
    std::cerr<<"PASS exact ABL selection, init_boot substitution, vendor_boot request/lookup and legacy extent controls; no tablet acceptance.\n";
}
int main(int argc,char** argv) {
    try {
        require(argc==2,"Pass --self-test or one regular image");
        if(std::string(argv[1])=="--self-test") { self_test(); return 0; }
        const std::filesystem::path image=argv[1];
        require(std::filesystem::is_regular_file(std::filesystem::symlink_status(image)),"Image must be a regular file, not a link or device");
        const auto bytes=std::filesystem::file_size(image);
        require(bytes>=4096 && bytes<=104857600,"Image must contain a complete header and fit the 100 MiB recovery slot");
        Header header{}; std::ifstream stream(image,std::ios::binary);
        require(static_cast<bool>(stream.read(reinterpret_cast<char*>(header.data()),header.size())),"Cannot read boot header");
        const auto result=inspect(header,bytes);
        const char* blocker=result.kernel==0 ? "slot-kernel-required" : result.version==4 ? "init-boot-ramdisk-substitution" :
            result.version==3 ? "vendor-boot-not-requested" : result.structural_candidate ? "physical-validation-required" : "legacy-image-incomplete";
        std::cout<<std::boolalpha<<"{\"schema_version\":1,\"evidence_class\":\"host-pinned-qualcomm-abl-branches\","
            "\"source_commit\":\"aef4e86045bd151a54d33bab8ae5e8e62462a8f4\","
            "\"branch_input_profile\":\"A/B,dynamic-partitions,dedicated-recovery,init-boot-present\",\"header_version\":"<<result.version
            <<",\"kernel_bytes\":"<<result.kernel<<",\"ramdisk_bytes\":"<<result.ramdisk
            <<",\"dtb_bytes\":"<<result.dtb<<",\"recovery_dtbo_bytes\":"<<result.dtbo
            <<",\"image_bytes\":"<<bytes<<",\"packed_payload_bytes\":"<<result.payload
            <<",\"abl_legacy_parser_extent_bytes\":"<<result.abl_extent
            <<",\"source_selects_recovery\":"<<result.selects_recovery
            <<",\"source_init_boot_substitution_condition\":"<<(result.selects_recovery ? (result.substitutes_init_boot ? "true" : "false") : "null")
            <<",\"source_vendor_boot_lookup_condition\":"<<(result.selects_recovery ? (result.requires_vendor_boot ? "true" : "false") : "null")
            <<",\"dedicated_kernel_branch_requests_vendor_boot\":"<<(result.selects_recovery ? (result.vendor_boot_requested ? "true" : "false") : "null")
            <<",\"structural_candidate\":"<<result.structural_candidate
            <<",\"status\":\""<<(result.structural_candidate ? "STRUCTURAL_CANDIDATE_UNVERIFIED" : "BLOCKED")
            <<"\",\"remaining_gate\":\""<<blocker<<'"'
            <<",\"validation\":{\"independent_boot_accepted\":false,\"oem_abl_binary_match\":false,"
            "\"module_closure\":false,\"firmware_closure\":false,\"physical_device\":false}}\n";
        return result.structural_candidate ? 0 : 1;
    } catch(const std::exception& error) { std::cerr<<"Boot-chain check: "<<error.what()<<'\n'; return 1; }
}
