// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include "partition_names.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <map>
#include <set>
#include <sys/mman.h>
#include <unistd.h>

namespace ure {
namespace {
struct Input { unsigned lun; const char* name; std::size_t bytes; const char* hash; };
// Exact Global OS3.0.303.0 inputs; other regions require their own reviewed pins.
constexpr Input inputs[]{
    {0,"gpt_main0.bin",24576,"1da4b242835fbcd932df2eb6adf3e0f7c8ddfef63bd911537e18640c5ea8d2a3"},
    {0,"gpt_backup0.bin",20480,"dc7cbba0d9fc9dca77dadc0ba30de1bac89d7f73d2983623bbc308414775c6f2"},
    {0,"gpt_both0.bin",45056,"02f20067bebf171fe89e85168f0c88572059b155eda7c772ab0ed13dc76c3a48"},
    {0,"rawprogram0.xml",9794,"c3ef4c7551eca76308f17b8fd5c26c78dbd8915fac6691d111a3c5649a312766"},
    {0,"patch0.xml",6263,"e0967a6505f8c0886f16d2863e5055d727f77e49b5a2850f0dee95a80daeb42c"},
    {1,"gpt_main1.bin",24576,"79b8c68363ea6b2a0eb715b4fb8e5743346ed5314efc597e3d185a2cf040be73"},
    {1,"gpt_backup1.bin",20480,"023496d9c84d5b11b2b3cad098e8e8b8302d128b21f55652013f28dc9107659e"},
    {1,"gpt_both1.bin",45056,"1bbb994ddd1ee3e1529923e1473c5f20e9df6e89254de96a9ac6f4ec4b436069"},
    {1,"rawprogram1.xml",2760,"e1dc3fbbd949ac55eff4a6752e86ff2ab3bf28906fc217c3e7b791b5b83e8aef"},
    {1,"patch1.xml",6263,"2e4a76d7e252e8a5e07a11238fa1d5ce137561c4da9da55ab84b84e8fc9af84b"},
    {2,"gpt_main2.bin",24576,"bca456daf67a56f130f242fa2a0c22daae3707c914b036eae541f39e18428504"},
    {2,"gpt_backup2.bin",20480,"a0b5218774cddbe2ebb742ac48dec94bb269b2f95abca207aa7a12911d63c0b8"},
    {2,"gpt_both2.bin",45056,"d84363672016f234a1ad7b9269617b36fa1f140a6ced2aa0c7b5b4a300e5c3c7"},
    {2,"rawprogram2.xml",2703,"3851068669a33b94bb41e14391f3662b1203a60c5e5e9d92617121ce55f000c9"},
    {2,"patch2.xml",6263,"7d70fb071723bb2284721e2a148b0a8dfb008e7e132329ec9e57ad1bdd54d143"},
    {3,"gpt_main3.bin",24576,"1f81ecd9f924ce32acdd3df2c0fc8ed0a6b5b21d891bbd4c810279c106564b09"},
    {3,"gpt_backup3.bin",20480,"ec47df9ce8086f4b3d9dcb7d2d9ccb037f0c74260b83b3f18fbca9dce2fef149"},
    {3,"gpt_both3.bin",45056,"c9e28a5ba868b8d83d563d3eba1614136fcd36a8b9cef98836661f859a94dad4"},
    {3,"rawprogram3.xml",1859,"05e90d8c89e46b4f58b9e11e7a93b193dedca372843864ec61b5dad94847641b"},
    {3,"patch3.xml",6263,"b5987d9616d44d4af421ffe00ee485afa98ddf402596f944f602cacb1db9022a"},
    {4,"gpt_main4.bin",24576,"3bce706209ec36960844d0e9e88c562aa3750822f2f6787db7688621bbfeb91b"},
    {4,"gpt_backup4.bin",20480,"dfe244e29e4da3cc3028f6834e0af30fbda00dce7f8bbf065079a22a2ec2b03a"},
    {4,"gpt_both4.bin",45056,"e9feda2d92ea06a5613976cf2e84cfa287c006fdd96721f93d0ed9d025d6511a"},
    {4,"rawprogram4.xml",20425,"de3a502fcc81a3a9b7390e2f6ab46336db489252f978f8208962daa138636169"},
    {4,"patch4.xml",6271,"eb4620b284f596a5438baa9152c56e6ebe90d3e5c47fd029a6e61e6240f302cb"},
    {5,"gpt_main5.bin",24576,"cace458a94e071cddee83a6b38901074857aa314265125ff4944e7cc9f07baea"},
    {5,"gpt_backup5.bin",20480,"d904d89dc10e256931e0def00148cb1a1997f53e18a89b08ff48fc3fd1ab6b86"},
    {5,"gpt_both5.bin",45056,"e8030ff6852806759734f9ff61fea68d835069f34f2aa935c4e5efd8767367e9"},
    {5,"rawprogram5.xml",2974,"89b60bc5279684e3be3da593870b3008d4e54eda551794f06742352793fb17d0"},
    {5,"patch5.xml",6263,"3f5bd9724b8a5cfe85cb2f934637871fc5d5f0e17e7f8cdbaa3e75021ebc14a4"}
};
constexpr std::uint32_t sector=4096;
constexpr std::array<unsigned,6> terminal{32,7,7,4,68,8};
std::uint64_t le(std::string_view bytes,std::size_t offset,unsigned count) {
    require(offset<=bytes.size() && count<=8 && count<=bytes.size()-offset,"invalid-stock-input","Stock integer is outside its input");
    std::uint64_t value=0; for(unsigned i=0;i<count;++i)value|=static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[offset+i]))<<(i*8); return value;
}
void put(std::string& bytes,std::size_t offset,std::uint64_t value,unsigned count) {
    require(offset<=bytes.size() && count<=8 && count<=bytes.size()-offset,"invalid-stock-input","Stock patch exceeds its input");
    for(unsigned i=0;i<count;++i)bytes[offset+i]=static_cast<char>((value>>(i*8))&255);
}
std::uint32_t crc(std::string_view bytes) {
    std::uint32_t value=UINT32_MAX;
    for(const char c:bytes) { value^=static_cast<unsigned char>(c); for(unsigned i=0;i<8;++i)value=(value>>1)^((value&1U) ? 0xedb88320U : 0U); }
    return value^UINT32_MAX;
}
std::string guid_bytes(const std::string& guid) {
    require(uuid(guid),"identity-unavailable","A nonzero original GUID is required");
    std::string plain; for(const char c:guid)if(c!='-')plain+=c;
    auto nibble=[](char c)->unsigned { if(c>='0' && c<='9')return static_cast<unsigned>(c-'0'); if(c>='a' && c<='f')return static_cast<unsigned>(c-'a'+10); return static_cast<unsigned>(c-'A'+10); };
    std::string bytes(16,'\0'); constexpr std::array<unsigned,16> order{3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15};
    for(unsigned i=0;i<16;++i)bytes[order[i]]=static_cast<char>((nibble(plain[i*2])<<4)|nibble(plain[i*2+1]));
    return bytes;
}
std::string label(std::string_view entry) {
    std::string name; bool ended=false;
    for(unsigned offset=56;offset<128;offset+=2) {
        const auto code=le(entry,offset,2);
        require(code<128 && (!ended || code==0),"invalid-stock-input","Unsupported stock label encoding");
        if(code==0)ended=true; else name+=static_cast<char>(code);
    }
    require(!name.empty(),"invalid-stock-input","Stock entry label is missing"); return name;
}
void sync_write(int fd,std::string_view bytes,std::uint64_t offset) {
    while(!bytes.empty()) {
        const auto n=::pwrite(fd,bytes.data(),bytes.size(),static_cast<off_t>(offset));
        if(n<0 && errno==EINTR)continue;
        require(n>0,"io-error","Cannot write reconstructed metadata"); offset+=static_cast<std::uint64_t>(n); bytes.remove_prefix(static_cast<std::size_t>(n));
    }
    require(::fsync(fd)==0,"io-error","Cannot sync reconstructed metadata");
}
Value reconstruct_table(std::uint64_t capacity,const std::vector<StorageRange>& ranges) {
    Fd temporary(::memfd_create("ure-stock-preview",MFD_CLOEXEC));
    require(temporary.get()>=0 && ::ftruncate(temporary.get(),static_cast<off_t>(capacity))==0,"validation-unavailable","Cannot validate sparse stock metadata");
    for(const auto& range:ranges)sync_write(temporary.get(),range.bytes,range.offset);
    auto table=gpt_inspect(temporary.get(),sector);
    require(table["healthy"]==true,"invalid-stock-layout","Reconstructed stock metadata is invalid (primary="+
        std::string(table["primary"]["valid"].asBool() ? "valid" : "invalid")+", backup="+
        std::string(table["backup"]["valid"].asBool() ? "valid" : "invalid")+", protective-MBR="+
        std::string(table["protective_mbr_valid"].asBool() ? "valid" : "invalid")+")"); return table;
}
}

std::vector<StorageRange> gpt_stock_regions(const fs::path& directory,std::uint64_t capacity,unsigned lun,
                                          const Value& identities,const std::string& profile,Value& source) {
    require(profile=="global-os3.0.303.0","wrong-profile","Stock reconstruction requires the pinned Global firmware profile");
    require(lun<terminal.size(),"invalid-lun","Stock reconstruction selects UFS LUN 0 through 5");
    require(capacity<=INT64_MAX && capacity%sector==0 && capacity/sector>11,"invalid-size","Stock capacity must be bounded and aligned to 4096 bytes");
    const auto sectors=capacity/sector; Root input(directory); std::map<std::string,std::string> files;
    source=Value(Json::objectValue); source["schema"]=1; source["format"]="ure-stock-gpt-source";
    source["firmware_profile"]=profile; source["lun"]=lun; source["capacity_bytes"]=Json::UInt64(capacity);
    source["source_archive_sha256"]="f811ae6255b7535d32f80548d800487a6494a87ddb4cca592799337fab24cd0d";
    source["inputs"]=Value(Json::arrayValue);
    for(const auto& pin:inputs)if(pin.lun==lun) {
        const auto st=input.stat(pin.name);
        require(S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_size>=0 && static_cast<std::uint64_t>(st.st_size)==pin.bytes,
            "invalid-stock-input","Stock input must be a regular single-link file with the pinned size");
        auto bytes=input.read(pin.name,pin.bytes);
        require(bytes.size()==pin.bytes && sha256(bytes)==pin.hash,"stock-source-mismatch","Stock input differs from its pinned OEM hash");
        Value item; item["filename"]=pin.name; item["bytes"]=Json::UInt64(pin.bytes); item["sha256"]=pin.hash; source["inputs"].append(item);
        files.emplace(pin.name,std::move(bytes));
    }
    const auto suffix=std::to_string(lun)+".bin"; const auto& main=files.at("gpt_main"+suffix); const auto& backup=files.at("gpt_backup"+suffix);
    require(files.at("gpt_both"+suffix)==main+backup,"invalid-stock-input","Combined OEM GPT does not agree with its separate inputs");
    auto entries=main.substr(2*sector,4*sector),primary=main.substr(sector,sector),secondary=backup.substr(4*sector,sector),mbr=main.substr(0,sector);
    require(backup.substr(0,4*sector)==entries && primary.substr(0,8)=="EFI PART" && secondary.substr(0,8)=="EFI PART" &&
        le(primary,12,4)==92 && le(primary,84,4)==128 && le(primary,40,8)==6 && le(primary,72,8)==2,
        "invalid-stock-input","Unsupported OEM GPT template geometry");
    const auto count=le(primary,80,4);
    require(count==(lun==4 ? 96U : 32U) && terminal[lun]<=count,"invalid-stock-input","OEM table count differs from the reviewed patch contract");
    std::map<std::string,Value> original;
    if(!identities.isNull()) {
        require(identities["healthy"]==true && identities["sector_bytes"].isUInt() && identities["sector_bytes"].asUInt()==sector && identities["bytes"].isUInt64() &&
            identities["bytes"].asUInt64()==capacity && identities["partitions"].isArray() && identities["disk_guid"].isString() && uuid(identities["disk_guid"].asString()),
            "identity-unavailable","Stock execution needs a healthy original GPT with matching capacity and sectors");
        Value records=identities["partitions"];
        if(identities["reserved_records"].isArray())for(const auto& part:identities["reserved_records"])records.append(part);
        for(const auto& part:records) {
            require(part["label"].isString() && !part["label"].asString().empty() && part["partuuid"].isString() && uuid(part["partuuid"].asString()) &&
                original.emplace(part["label"].asString(),part).second,
                "ambiguous-identity","Original partition labels are missing or ambiguous");
        }
        primary.replace(56,16,guid_bytes(identities["disk_guid"].asString())); secondary.replace(56,16,guid_bytes(identities["disk_guid"].asString()));
    }
    std::set<std::string> expected;
    for(unsigned index=0;index<terminal[lun];++index) {
        const auto offset=static_cast<std::size_t>(index)*128; const auto name=label(std::string_view(entries).substr(offset,128)); expected.insert(name);
        if(!identities.isNull()) {
            const auto found=original.find(name);
            require(found!=original.end(),"identity-unavailable","A stock partition is missing; supply its verified same-target original GPT backup");
            entries.replace(offset+16,16,guid_bytes(found->second["partuuid"].asString()));
        }
    }
    for(const auto& [name,part]:original) {
        (void)part;
        const auto role=os_partition_role(name);
        require(expected.count(name)!=0 || role=="linux" || role=="windows" || role=="esp",
            "protected-partition","Unknown extra partitions require a separate ownership and migration plan");
    }
    put(entries,(terminal[lun]-1)*128+40,sectors-6,8);
    const auto sum=crc(std::string_view(entries).substr(0,static_cast<std::size_t>(count)*128));
    for(auto* header:{&primary,&secondary}) { put(*header,48,sectors-6,8); put(*header,88,sum,4); put(*header,16,0,4); }
    put(primary,32,sectors-1,8); put(secondary,24,sectors-1,8); put(secondary,72,sectors-5,8);
    put(primary,16,crc(std::string_view(primary).substr(0,92)),4); put(secondary,16,crc(std::string_view(secondary).substr(0,92)),4);
    // The OEM template PMBR also carries a template capacity, independently of
    // patch XML. A protective MBR must cover the actual logical-sector range.
    require(static_cast<unsigned char>(mbr[450])==0xee && le(mbr,454,4)==1,"invalid-stock-input","OEM protective MBR is unsupported");
    put(mbr,458,std::min<std::uint64_t>(sectors-1,UINT32_MAX),4);
    std::vector<StorageRange> result{{"primary_table",2*sector,entries},{"primary_header",sector,primary},
        {"backup_table",(sectors-5)*sector,entries},{"backup_header",(sectors-1)*sector,secondary},{"protective_mbr",0,mbr}};
    source["desired_table"]=reconstruct_table(capacity,result); source["original_identity_sha256"]=identities.isNull() ? Value() : Value(sha256(json(identities)));
    source["identity_origin"]=identities.isNull() ? "OEM_TEMPLATE_PREVIEW_ONLY" : "VERIFIED_ORIGINAL_GPT";
    source["template_preview_only"]=identities.isNull(); source["physical_test_record"]=false; source["private_record"]=true;
    source["manifest_sha256"]=sha256(json(source)); return result;
}

Value gpt_stock_preview(const fs::path& inputs,std::uint64_t capacity,unsigned lun,const std::string& profile,const fs::path& destination) {
    Value source; const auto ranges=gpt_stock_regions(inputs,capacity,lun,Value(),profile,source);
    auto output=private_directory(destination,true); Value manifest=source; manifest["format"]="ure-stock-gpt-preview";
    manifest["regions"]=Value(Json::arrayValue); manifest["restore_authorized"]=false;
    for(const auto& range:ranges) {
        auto fd=output.open(range.name+".bin",O_RDWR|O_CREAT|O_EXCL,0600); sync_write(fd.get(),range.bytes,0);
        require(sha256(fd.get())==sha256(range.bytes),"verification-error","Stock preview metadata readback differs");
        Value item; item["name"]=range.name; item["offset"]=Json::UInt64(range.offset); item["bytes"]=Json::UInt64(range.bytes.size()); item["sha256"]=sha256(range.bytes);
        manifest["regions"].append(item);
    }
    manifest.removeMember("manifest_sha256"); manifest["manifest_sha256"]=sha256(json(manifest)); output.save_record("manifest.json",manifest);
    require(::fsync(output.fd())==0,"io-error","Cannot sync stock preview directory"); return manifest;
}
} // namespace ure
