// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "uke.h"
#include <algorithm>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>

namespace boot_fixture {
inline constexpr const char* partition_uuid="12345678-1234-5678-9abc-def012345678";
inline std::string variable(const std::string& name) { return name+"-8be4df61-93ca-11d2-aa0d-00e098032b8c"; }
inline void check(bool condition,const std::string& message) { if(!condition)throw std::runtime_error(message); }
inline void put(const ure::fs::path& path,const std::string& bytes) {
    ure::fs::create_directories(path.parent_path()); std::ofstream out(path,std::ios::binary); out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    check(out.good(),"Cannot write EFI fixture"); out.close(); check(::chmod(path.c_str(),0600)==0,"Cannot protect EFI fixture");
}
inline void le(std::string& bytes,std::size_t at,std::uint64_t value,unsigned count) {
    check(at+count<=bytes.size(),"Fixture field out of range"); for(unsigned i=0;i<count;++i)bytes[at+i]=static_cast<char>((value>>(i*8))&255);
}
inline std::string utf16(const std::string& ascii) {
    std::string result; for(const char c:ascii) { result.push_back(c); result.push_back('\0'); } result.append(2,'\0'); return result;
}
inline std::string pe() {
    std::string result(1024,'\0'); result.replace(0,2,"MZ"); le(result,60,128,4); result.replace(128,4,"PE\0\0",4);
    le(result,132,0xaa64,2); le(result,134,1,2); le(result,148,240,2); le(result,152,0x20b,2); le(result,220,10,2); return result;
}
inline std::string load_option(const std::string& path,const std::string& title) {
    std::string hd(42,'\0'); hd[0]=4; hd[1]=1; le(hd,2,42,2); le(hd,4,1,4); le(hd,8,2048,8); le(hd,16,65536,8);
    le(hd,24,0x12345678,4); le(hd,28,0x1234,2); le(hd,30,0x5678,2);
    const unsigned char tail[]={0x9a,0xbc,0xde,0xf0,0x12,0x34,0x56,0x78};
    for(unsigned i=0;i<8;++i)hd[32+i]=static_cast<char>(tail[i]);
    hd[40]=2; hd[41]=2;
    std::string node(4,'\0'); node[0]=4; node[1]=4; node+=utf16(path); le(node,2,node.size(),2);
    std::string end(4,'\0'); end[0]=0x7f; end[1]=static_cast<char>(0xff); le(end,2,4,2);
    std::string result(10,'\0'); le(result,0,7,4); le(result,4,1,4); le(result,8,hd.size()+node.size()+end.size(),2);
    return result+utf16(title)+hd+node+end+"opaque-fixture-options";
}
inline ure::Value request(const std::string& target="linux",const std::string& number="0001",const std::string& fallback="0000") {
    ure::Value result; result["schema"]=1; result["target"]=target; result["boot_option"]=number; result["fallback_option"]=fallback;
    result["esp_partuuid"]=partition_uuid; result["profile"]="global-os3.0.303.0"; result["model"]="poco-pad-x1"; return result;
}
inline void create(const ure::fs::path& base) {
    const auto vars=base/"variables",esp=base/"esp"; ure::fs::create_directories(vars); ure::fs::create_directories(esp);
    check(::chmod(vars.c_str(),0700)==0,"Cannot protect EFI variable directory");
    ure::Value marker; marker["schema"]=1; marker["kind"]="ure-uefi-variable-fixture"; put(vars/".ure-efi-fixture.json",ure::json(marker));
    std::string order(12,'\0'); le(order,0,7,4); for(unsigned i=0;i<4;++i)le(order,4+i*2,i,2); put(vars/variable("BootOrder"),order);
    const std::vector<std::pair<std::string,std::string>> loaders{{"EFI/UKE/android.efi","Android fixture"},{"EFI/Linux/arch.efi","Arch fixture"},
        {"EFI/Microsoft/Boot/bootmgfw.efi","Windows fixture"},{"EFI/Linux/fedora.efi","Fedora fallback fixture"}};
    for(std::size_t i=0;i<loaders.size();++i) {
        const auto& [path,title]=loaders[i]; put(esp/path,pe()); auto efi_path="\\"+path; std::replace(efi_path.begin(),efi_path.end(),'/','\\');
        put(vars/variable("Boot000"+std::to_string(i)),load_option(efi_path,title));
    }
    put(base/"request.json",ure::json(request()));
}
} // namespace boot_fixture
