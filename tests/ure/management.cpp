// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unistd.h>
using namespace std::string_literals;
namespace {
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
template<class F> void reject(F function,const std::string& code) { try { function(); } catch(const ure::Error& error) { check(error.code==code,("Unexpected error: "+error.code).c_str()); return; } throw std::runtime_error("Missing rejection: "+code); }
void write(const ure::fs::path& path,const std::string& value) { ure::fs::create_directories(path.parent_path()); std::ofstream out(path,std::ios::binary); out.write(value.data(),static_cast<std::streamsize>(value.size())); check(out.good(),"Cannot create fixture"); }
void le(std::string& bytes,std::size_t at,std::uint64_t number,unsigned size) { for(unsigned i=0;i<size;++i)bytes[at+i]=static_cast<char>((number>>(8*i))&255); }
std::string stream_command(unsigned command,const std::vector<std::pair<unsigned,std::string>>& attributes) {
    std::string bytes(10,'\0'); le(bytes,4,command,2);
    for(const auto& [type,value]:attributes) { const auto at=bytes.size(); bytes.resize(at+4); le(bytes,at,type,2); le(bytes,at+2,value.size(),2); bytes+=value; }
    le(bytes,0,bytes.size()-10,4); std::uint32_t crc=0;
    for(const auto byte:bytes) { crc^=static_cast<unsigned char>(byte); for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1) ? 0x82f63b78U : 0); }
    le(bytes,6,crc,4); return bytes;
}
std::string archive(const std::string& release) {
    std::string out;
    for(const auto& path:{"usr/lib/modules/"+release+"/kernel/test.ko",std::string("TRAILER!!!")}) {
        std::string header="070701"; for(unsigned i=0;i<13;++i) { std::ostringstream value; value<<std::hex<<std::setw(8)<<std::setfill('0')<<(i==1 ? 0100644U : i==4 ? 1U : i==11 ? static_cast<unsigned>(path.size()+1) : 0U); header+=value.str(); }
        out+=header+path+'\0'; while(out.size()%4)out+='\0';
    } return out;
}
std::string module(const std::string& release) {
    const std::string strings("\0.shstrtab\0.modinfo\0",20); const auto metadata="vermagic="+release+" SMP\0"s;
    std::string out(256,'\0'); out.replace(0,4,"\x7f" "ELF"); out[4]=2; out[5]=1; out[6]=1;
    le(out,16,1,2); le(out,18,183,2); le(out,20,1,4); le(out,40,64,8); le(out,52,64,2); le(out,58,64,2); le(out,60,3,2); le(out,62,1,2);
    le(out,128,1,4); le(out,132,3,4); le(out,152,256,8); le(out,160,strings.size(),8);
    le(out,192,11,4); le(out,196,1,4); le(out,216,256+strings.size(),8); le(out,224,metadata.size(),8); out+=strings+metadata; return out;
}
std::string device_tree() {
    auto be=[](std::string& data,std::size_t at,std::uint32_t value) { for(unsigned i=0;i<4;++i)data[at+i]=static_cast<char>((value>>(8*(3-i)))&255); };
    const auto compatible="ure,virt-fixture\0"s; std::string structure(20,'\0'); be(structure,0,1); be(structure,8,3); be(structure,12,static_cast<std::uint32_t>(compatible.size()));
    structure+=compatible; while(structure.size()%4)structure+='\0'; structure.resize(structure.size()+8); be(structure,structure.size()-8,2); be(structure,structure.size()-4,9);
    const auto strings="compatible\0"s; std::string bytes(56,'\0'); be(bytes,0,0xd00dfeed); be(bytes,4,static_cast<std::uint32_t>(bytes.size()+structure.size()+strings.size()));
    be(bytes,8,56); be(bytes,12,static_cast<std::uint32_t>(56+structure.size())); be(bytes,16,40); be(bytes,20,17); be(bytes,24,16);
    be(bytes,32,static_cast<std::uint32_t>(strings.size())); be(bytes,36,static_cast<std::uint32_t>(structure.size())); return bytes+structure+strings;
}
std::string uki(const std::string& image) {
    const std::vector<std::pair<std::string,std::string>> sections{{".linux",image},{".osrel","ID=arch\n"},{".uname","1"},{".cmdline","root=UUID=test-root"},{".initrd",archive("1")},{".dtb",device_tree()}};
    std::string bytes(1024,'\0'); bytes.replace(0,2,"MZ"); le(bytes,60,128,4); bytes.replace(128,4,"PE\0\0",4); le(bytes,132,0xaa64,2); le(bytes,134,sections.size(),2); le(bytes,148,112,2); le(bytes,152,0x20b,2);
    for(std::size_t i=0;i<sections.size();++i) { const auto at=264+i*40; bytes.replace(at,sections[i].first.size(),sections[i].first); le(bytes,at+8,sections[i].second.size(),4);
        le(bytes,at+16,sections[i].second.size(),4); le(bytes,at+20,bytes.size(),4); bytes+=sections[i].second; }
    return bytes;
}
bool finding(const ure::Value& report,const std::string& code) { for(const auto& item:report["findings"])if(item["code"]==code)return true; return false; }
}
int main() {
    std::array<char,32> temporary{}; const std::string pattern="/tmp/ure-management-XXXXXX"; std::copy(pattern.begin(),pattern.end(),temporary.begin());
    const auto* path=::mkdtemp(temporary.data()); if(!path)return 1; const ure::fs::path work(path);
    try {
        std::string stream_header="btrfs-stream\0"s; stream_header.resize(17); le(stream_header,13,1,4);
        const auto stream_start=stream_command(1,{{15,"snapshot"},{1,std::string(16,'\1')},{2,std::string(8,'\0')}});
        auto inspect_stream=[&](const std::string& commands) {
            write(work/"stream.bin",stream_header+stream_start+commands+stream_command(21,{}));
            ure::Fd fd(::open((work/"stream.bin").c_str(),O_RDONLY|O_CLOEXEC)); return ure::btrfs_stream_check(fd.get());
        };
        check(inspect_stream(stream_command(19,{{15,""},{6,std::string(8,'\0')},{7,std::string(8,'\0')}}))["structural_validation"]==true,
            "Kernel root metadata with an empty path was rejected");
        check(inspect_stream(stream_command(18,{{15,"."},{5,std::string(8,'\0')}}))["structural_validation"]==true,"Root metadata alias was rejected");
        for(const auto& unsafe:{std::string(""),std::string("."),std::string("../outside"),std::string("/outside")})
            reject([&] { inspect_stream(stream_command(3,{{15,unsafe},{3,std::string(8,'\0')}})); },"invalid-btrfs-stream");
        reject([&] { inspect_stream(stream_command(12,{{15,""}})); },"invalid-btrfs-stream");
        const auto tree=work/"root",esp_path=work/"esp"; ure::fs::create_directories(esp_path); write(tree/"usr/lib/os-release","ID=arch\nNAME=Arch\n");
        write(tree/"etc/fstab","UUID=test-root / ext4 defaults 0 1\n"); ure::fs::create_symlink("../usr/lib/os-release",tree/"etc/os-release");
        ure::fs::create_symlink("usr/lib",tree/"lib"); write(tree/"usr/lib/modules/1/modules.dep","kernel/test.ko:\n");
        for(const auto* index:{"modules.alias","modules.symbols","modules.builtin"})write(tree/(std::string("usr/lib/modules/1/")+index),"");
        write(tree/"usr/lib/modules/1/kernel/test.ko",module("1"));
        std::string image(64,'\0'); le(image,16,64,8); le(image,56,0x644d5241,4); write(tree/"boot/vmlinuz-1",image); write(tree/"boot/initramfs-1.img",archive("1"));
        write(tree/"boot/loader/entries/arch.conf","title Arch\nversion 1\nlinux /vmlinuz-1\ninitrd /initramfs-1.img\noptions root=UUID=test-root\n");
        ure::Root root(tree),esp(esp_path); check(ure::linux_detect(root)["distribution"]["ID"]=="arch","Contained os-release alias did not resolve");
        reject([&] { root.open_resolved("etc/os-release",O_RDWR); },"read-only-resolver"); write(work/"secret","outside-root"); ure::fs::create_symlink("../../secret",tree/"etc/escape");
        reject([&] { root.read_resolved("etc/escape"); },"path-unavailable");
        auto audit=ure::linux_boot_audit(root,&esp); check(audit["error_count"].asUInt()==0 && audit["kernels"][0]["module_metadata_checked"].asUInt()==1,"Valid native assets were rejected");
        check(audit["boot_validated"]==false && audit["physical_test_record"]==false,"Metadata audit claimed hardware success");
        write(tree/"usr/lib/modules/1/kernel/test.ko",module("2")); check(finding(ure::linux_boot_audit(root),"module-vermagic-mismatch"),"Wrong kernel module release passed");
        write(tree/"usr/lib/modules/1/kernel/test.ko",module("1")); write(tree/"boot/initramfs-1.img",archive("2"));
        check(finding(ure::linux_boot_audit(root),"initramfs-version-mismatch"),"Wrong initramfs release passed");
        write(tree/"boot/initramfs-1.img",archive("1")); write(esp_path/"loader/entries/esp.conf","linux /vmlinuz-1\ninitrd /initramfs-1.img\noptions root=UUID=test-root\n");
        check(finding(ure::linux_boot_audit(root,&esp),"path-unavailable"),"ESP entry borrowed a component from the unrelated root boot directory");
        ure::fs::remove(esp_path/"loader/entries/esp.conf");
        write(tree/"usr/lib/modules/1/modules.dep","kernel/test.ko: kernel/missing.ko\n"); check(finding(ure::linux_boot_audit(root),"missing-module-dependency"),"Missing indexed dependency passed");
        write(tree/"usr/lib/modules/1/modules.dep","kernel/test.ko:\n"); write(tree/"boot/vmlinuz-1","not-a-kernel");
        check(finding(ure::linux_boot_audit(root),"unrecognized-kernel"),"Existing invalid kernel filename passed");
        write(tree/"boot/vmlinuz-1",image); write(esp_path/"EFI/Linux/arch.efi",uki(image));
        audit=ure::linux_boot_audit(root,&esp); check(audit["error_count"].asUInt()==0 && audit["ukis"][0]["metadata"]["device_tree"]["structure_valid"]==true,"Valid PE UKI and FDT were rejected");
        auto bad_uki=uki(image); le(bad_uki,264+40+20,1024,4); write(esp_path/"EFI/Linux/arch.efi",bad_uki);
        check(finding(ure::linux_boot_audit(root,&esp),"invalid-uki"),"Overlapping UKI sections passed"); ure::fs::remove(esp_path/"EFI/Linux/arch.efi");
        auto shell=module("1"); le(shell,16,3,2); write(tree/"usr/bin/bash",shell); ure::fs::create_symlink("usr/bin",tree/"bin");
        check(ure::linux_detect(root)["architecture"]=="aarch64","Userspace architecture alias was not inspected"); le(shell,18,62,2); write(tree/"usr/bin/bash",shell);
        check(finding(ure::linux_boot_audit(root),"userspace-kernel-architecture"),"Userspace/kernel architecture mismatch passed"); ure::fs::remove(tree/"usr/bin/bash");
        std::string index; for(unsigned i=0;i<257;++i) { std::ostringstream name; name<<"kernel/extra-"<<std::setw(3)<<std::setfill('0')<<i<<".ko";
            write(tree/("usr/lib/modules/1/"+name.str()),module(i==256 ? "2" : "1")); index+=name.str()+":\n"; }
        write(tree/"usr/lib/modules/1/modules.dep",index); audit=ure::linux_boot_audit(root);
        check(audit["kernels"][0]["module_metadata_checked"].asUInt()==257 && audit["kernels"][0]["modules"].size()==256 && finding(audit,"module-vermagic-mismatch"),"A module error beyond the report sample escaped inspection");
        write(work/"empty.img",std::string(32*1024*1024,'\0')); auto target=ure::storage_image(work/"empty.img",512); ure::Root system("/");
        ure::Value request; request["schema"]=1; request["action"]="format"; request["filesystem"]="ext4";
        reject([&] { ure::filesystem_operation_plan(system,target,request,"fixture-profile"); },"erase-confirmation-required");
        request["erase_confirmed"]=true; request["unexpected"]=true;
        reject([&] { ure::filesystem_operation_plan(system,target,request,"fixture-profile"); },"invalid-filesystem-request"); request.removeMember("unexpected");
        const auto plan=ure::filesystem_operation_plan(system,target,request,"fixture-profile");
        reject([&] { ure::filesystem_operation_execute(system,target,plan,work/"bad-confirm","bad"); },"confirmation-required");
        check(!ure::fs::exists(work/"bad-confirm"),"Failed confirmation created a journal");
        reject([&] { ure::management_dispatch({"linux","audit","--root",tree.string(),"--confirm","unused"}); },"invalid-options");
        reject([&] { ure::management_dispatch({"filesystem","capabilities","--profile","unused"}); },"invalid-options");
        check(ure::storage_preflight(system,target,"fixture-profile")["live_job_eligible"]==false,"An image forged live firmware proof");
        std::cout<<"Installed-system aliases, module/boot mismatch detection and management authorization fixtures passed.\n";
        ure::fs::remove_all(work); return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; ure::fs::remove_all(work); return 1; }
}
