// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <array>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <cstring>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace {
void check(bool value,const std::string& message) { if(!value)throw std::runtime_error(message); }
template<class F> void reject(F function,const std::string& code) {
    try { function(); } catch(const ure::Error& e) { check(e.code==code,"Wrong rejection: "+e.code+", expected "+code); return; }
    throw std::runtime_error("Expected rejection: "+code);
}
void write(const ure::fs::path& path,const std::string& data) {
    ure::fs::create_directories(path.parent_path()); std::ofstream file(path,std::ios::binary); file.write(data.data(),static_cast<std::streamsize>(data.size())); check(file.good(),"Fixture write failed");
}
void le(std::string& data,std::size_t offset,std::uint64_t value,unsigned size) {
    for(unsigned i=0;i<size;++i)data[offset+i]=static_cast<char>((value>>(i*8))&255);
}
std::uint32_t crc(const std::string& bytes) {
    std::uint32_t result=0xffffffff;
    for(const auto b:bytes) { result^=static_cast<unsigned char>(b); for(unsigned i=0;i<8;++i)result=(result>>1)^((result&1) ? 0xedb88320 : 0); }
    return result^0xffffffff;
}
std::string gpt(unsigned sector) {
    const unsigned count=128,table_sectors=16384/sector,backup_table=count-1-table_sectors;
    std::string disk(sector*count,'\0'),entries(16384,'\0');
    disk[510]=0x55; disk[511]=static_cast<char>(0xaa); disk[450]=static_cast<char>(0xee);
    le(disk,454,1,4); le(disk,458,count-1,4);
    for(unsigned i=0;i<32;++i)entries[i]=static_cast<char>(i+1);
    le(entries,32,2+table_sectors+2,8); le(entries,40,backup_table-2,8); entries[56]='u'; entries[58]='k'; entries[60]='e';
    disk.replace(sector*2,entries.size(),entries); disk.replace(sector*backup_table,entries.size(),entries);
    for(unsigned lba:{1U,count-1}) {
        std::string header(sector,'\0'); header.replace(0,8,"EFI PART");
        le(header,8,0x10000,4); le(header,12,92,4); le(header,24,lba,8); le(header,32,count-lba,8);
        le(header,40,2+table_sectors,8); le(header,48,backup_table-1,8); for(unsigned i=0;i<16;++i)header[56+i]=static_cast<char>(i+41);
        le(header,72,lba==1 ? 2 : backup_table,8); le(header,80,128,4); le(header,84,128,4); le(header,88,crc(entries),4);
        le(header,16,crc(header.substr(0,92)),4); disk.replace(lba*sector,sector,header);
    }
    return disk;
}
}
int main() {
    std::array<char,32> temp{}; const std::string pattern="/tmp/ure-tests-XXXXXX"; std::copy(pattern.begin(),pattern.end(),temp.begin());
    const char* directory=::mkdtemp(temp.data()); if(!directory)return 1; const ure::fs::path work(directory);
    try {
        const auto tree=work/"root"; ure::fs::create_directory(tree);
        write(tree/"usr/lib/os-release","ID=fedora\nNAME=\"Fedora Linux\"\nVERSION_ID=rawhide\n");
        write(tree/"etc/fstab","UUID=example / ext4 defaults 0 1\n");
        write(tree/"etc/crypttab","root UUID=example none luks\n");
        write(tree/"boot/vmlinuz-1","kernel"); write(tree/"boot/initramfs-1.img","initramfs");
        ure::fs::create_directories(tree/"usr/lib/modules/1");
        write(tree/"boot/loader/entries/fedora.conf","title Fedora\nlinux /vmlinuz-1\ninitrd /initramfs-1.img\noptions root=UUID=example\n");
        ure::Root root(tree);
        const auto linux_info=ure::linux_detect(root);
        check(linux_info["detected"].asBool() && linux_info["distribution"]["ID"]=="fedora","Distro discovery failed");
        check(linux_info["kernels"].size()==1 && linux_info["kernels"][0]["components_present"].asBool(),"Kernel consistency failed");
        check(linux_info["boot_entries"][0]["components_present"].asBool(),"BLS discovery failed");
        ure::fs::remove(tree/"boot/initramfs-1.img");
        check(!ure::linux_detect(root)["boot_entries"][0]["components_present"].asBool(),"Missing initramfs was accepted");
        reject([]{ure::parse_json("{\"schema\":1,\"schema\":2}");},"invalid-json");
        reject([]{ure::parse_json("{\"a\":1} trailing");},"invalid-json");
        reject([&]{root.read("../outside");},"invalid-path");
        write(work/"outside","private"); ure::fs::create_symlink("../../outside",tree/"etc/escape");
        reject([&]{root.read("etc/escape");},"path-unavailable");
        reject([&]{root.atomic_save("etc/escape","new",ure::sha256("private"));},"path-unavailable");
        auto plan=ure::transaction_plan(root,"etc/fstab","UUID=example / ext4 ro 0 1\n","fixture-profile");
        plan=ure::parse_json(ure::json(plan));
        const auto confirmation=plan["plan_sha256"].asString();
        reject([&]{ure::transaction_run(root,plan,work/"wrong-confirm","incorrect");},"confirmation-required");
        ::chmod((tree/"etc/fstab").c_str(),0640);
        reject([&]{ure::transaction_run(root,plan,work/"stale","incorrect");},"stale-plan");
        const char attribute[]="fixture";
        const bool xattrs=::setxattr((tree/"etc/fstab").c_str(),"user.ure",attribute,sizeof(attribute),0)==0;
        plan=ure::transaction_plan(root,"etc/fstab","UUID=example / ext4 ro 0 1\n","fixture-profile");
        const auto state=ure::transaction_run(root,ure::parse_json(ure::json(plan)),work/"transaction",plan["plan_sha256"].asString());
        check(state["state"]=="COMMITTED" && root.read("etc/fstab").find(" ro ")!=std::string::npos,"Transaction did not commit");
        check((root.stat("etc/fstab").st_mode & 07777)==0640,"File mode was not preserved");
        if(xattrs) { char value[32]{}; check(::getxattr((tree/"etc/fstab").c_str(),"user.ure",value,sizeof(value))==sizeof(attribute),"File xattr was lost"); }
        const auto rollback=ure::transaction_rollback(root,work/"transaction",plan["plan_sha256"].asString());
        check(rollback["state"]=="ROLLED_BACK" && root.read("etc/fstab").find(" defaults ")!=std::string::npos,"Rollback failed");
        auto stale=ure::transaction_plan(root,"etc/fstab","new\n","fixture-profile"); write(tree/"etc/fstab","unrelated\n");
        reject([&]{ure::transaction_run(root,stale,work/"changed",stale["plan_sha256"].asString());},"stale-plan");
        write(tree/"etc/fstab","a / ext4 defaults\nb / ext4 defaults\n");
        check(!ure::config_validate(root,"etc/fstab","fstab")["syntax_valid"].asBool(),"Duplicate fstab target accepted");
        write(tree/"etc/editor","one\nTürkçe\nthree\n");
        ure::Editor editor(root,"etc/editor","fixture-profile");
        editor.line(0,"first"); editor.insert(1,"inserted"); editor.erase(2); editor.undo(); editor.redo();
        check(editor.replace("three","last")==1 && editor.text()=="first\ninserted\nlast\n","Editor mutation/history failed");
        editor.undo(); check(editor.text().find("three")!=std::string::npos,"Replace undo failed");
        editor.redo(); const auto edit_plan=editor.plan(root); ure::validate_plan(root,edit_plan);
        write(tree/"etc/editor","external change\n"); reject([&]{editor.plan(root);},"stale-source");
        check(!ure::utf8(std::string("\xc0\x80",2)) && !ure::utf8(std::string("\xed\xa0\x80",3)) &&
            !ure::utf8(std::string("\xf4\x90\x80\x80",4)) && !ure::utf8(std::string("\xe2\x82",2)) && ure::utf8("Türkçe"),"UTF-8 validation failed");
        write(tree/"etc/empty.json",""); reject([&]{ure::config_validate(root,"etc/empty.json","json");},"invalid-json");
        for(unsigned sector:{512U,4096U}) {
            auto bytes=gpt(sector); write(work/"gpt",bytes); ure::Fd fixture(::open((work/"gpt").c_str(),O_RDONLY));
            const auto valid=ure::gpt_inspect(fixture.get(),sector); check(valid["healthy"].asBool() && valid["partitions"].size()==1,"Valid GPT rejected");
            bytes[sector+16]^=1; write(work/"gpt",bytes);
            const auto broken=ure::gpt_inspect(fixture.get(),sector); check(!broken["healthy"].asBool() && broken["state"]=="INSPECTION_REQUIRED","Corrupt primary GPT accepted");
            bytes=gpt(sector); bytes[510]=0; write(work/"gpt",bytes);
            check(!ure::gpt_inspect(fixture.get(),sector)["healthy"].asBool(),"Missing protective MBR accepted");
            bytes=gpt(sector); bytes[466]=static_cast<char>(0x83); write(work/"gpt",bytes);
            check(!ure::gpt_inspect(fixture.get(),sector)["healthy"].asBool(),"Hybrid MBR accepted as protective-only GPT");
            bytes=gpt(sector); le(bytes,458,10,4); write(work/"gpt",bytes);
            check(!ure::gpt_inspect(fixture.get(),sector)["healthy"].asBool(),"Incorrect protective MBR size accepted");
            bytes=gpt(sector); le(bytes,sector+72,4,8); le(bytes,sector+16,0,4); le(bytes,sector+16,crc(bytes.substr(sector,92)),4); write(work/"gpt",bytes);
            check(!ure::gpt_inspect(fixture.get(),sector)["healthy"].asBool(),"GPT table overlapping usable LBAs accepted");
        }
        std::string ext4(4096,'\0'); ext4[1080]=0x53; ext4[1081]=static_cast<char>(0xef);
        for(unsigned i=0;i<16;++i)ext4[1128+i]=static_cast<char>(i);
        write(work/"ext4",ext4);
        ure::Fd ext4_fd(::open((work/"ext4").c_str(),O_RDONLY));
        check(ure::filesystem_probe(ext4_fd.get())["uuid"]=="00010203-0405-0607-0809-0a0b0c0d0e0f","Filesystem UUID byte order is incorrect");
        reject([]{ure::run_tool("sh",{"-c","true"});},"tool-rejected");
        const auto system_tree=work/"system";
        write(system_tree/"sys/devices/test/block/sda/sda1/uevent","MAJOR=8\nMINOR=1\nPARTNAME=uke_linux\nPARTUUID=12345678-1234-1234-1234-123456789abc\n");
        write(system_tree/"sys/devices/test/block/sda/sda1/partition","1\n");
        write(system_tree/"sys/devices/test/block/sda/sda1/size","256\n");
        write(system_tree/"sys/devices/test/block/sda/queue/logical_block_size","4096\n");
        write(system_tree/"proc/self/mountinfo","1 0 8:1 / /mnt/fixture ro - ext4 /dev/sda1 ro\n");
        ure::fs::create_directories(system_tree/"sys/class/block");
        ure::fs::create_symlink("../../devices/test/block/sda/sda1",system_tree/"sys/class/block/sda1");
        ure::Root system(system_tree); const auto graph=ure::storage_graph(system);
        check(graph["objects"].size()==1 && graph["objects"][0]["parent_lun_name"]=="sda" &&
            graph["objects"][0]["bytes"].asUInt64()==131072 && graph["objects"][0]["logical_sector_bytes"].asUInt64()==4096 && graph["edges"].size()==1,"Storage Graph identity or units failed");
        const auto partition_path=system_tree/"sys/devices/test/block/sda/sda1";
        check(graph["objects"][0]["dependencies_available"]==false,"Missing partition holders were accepted");
        ure::fs::create_directory(partition_path/"holders");
        check(ure::storage_graph(system)["objects"][0]["dependencies_available"]==true,"A kernel partition incorrectly requires a slaves directory");
        ure::fs::remove(partition_path/"partition");
        check(ure::storage_graph(system)["objects"][0]["dependencies_available"]==false,"A whole disk without slaves was accepted");
        ure::fs::create_directory(partition_path/"slaves");
        check(ure::storage_graph(system)["objects"][0]["dependencies_available"]==true,"Whole-disk dependency directories were rejected");
        ure::fs::remove(partition_path/"holders");
        check(ure::storage_graph(system)["objects"][0]["dependencies_available"]==false,"A whole disk without holders was accepted");
        ure::fs::create_directory(partition_path/"holders");
        write(partition_path/"partition","1\n");
        ure::fs::remove(partition_path/"slaves");
        write(partition_path/"slaves","invalid-directory\n");
        reject([&]{ure::storage_graph(system);},"path-unavailable");
        ure::fs::remove(partition_path/"slaves");
        const auto usage=ure::storage_usage(system,graph["objects"][0]["stable_id"].asString());
        check(usage["quiescent_observed"]==false && usage["coverage"]["processes"]==false && usage["coverage"]["usb"]==false,
            "A synthetic proc/configfs tree was accepted as complete ownership evidence");
        write(system_tree/"sys/devices/test/block/sda/sda2/uevent","MAJOR=8\nMINOR=2\nPARTNAME=uke_linux\nPARTUUID=12345678-1234-1234-1234-123456789ABC\n");
        ure::fs::create_symlink("../../devices/test/block/sda/sda2",system_tree/"sys/class/block/sda2");
        reject([&]{ure::storage_graph(system);},"ambiguous-identity");
        write(system_tree/"sys/devices/test/block/sda/sda2/uevent","MAJOR=8\nMINOR=2\nPARTNAME=uke_linux\nPARTUUID=00000000-0000-0000-0000-000000000000\n");
        reject([&]{ure::storage_graph(system);},"ambiguous-identity");
        ure::fs::remove(system_tree/"sys/class/block/sda2");
        write(system_tree/"sys/fs/pstore/console-ramoops-0","ordinary private unit token SECRET_UNIT_123\n");
        write(system_tree/"proc/cmdline","androidboot.serialno=SECRET_UNIT_123\n");
        const auto report=ure::json(ure::public_report(system));
        check(report.find("SECRET_UNIT_123")==report.npos && report.find("12345678-1234-1234-1234-123456789abc")==report.npos,"Public report leaked private identifiers or pstore");
        check(system.read("sys/fs/pstore/console-ramoops-0").find("SECRET_UNIT_123")!=std::string::npos,"Report cleared crash evidence");
        ure::fs::create_symlink("../../../../outside",system_tree/"sys/class/block/escape");
        reject([&]{ure::storage_graph(system);},"invalid-sysfs");
        const auto clean=ure::redact("password=secret\nssid=private\nserialno=123\n01:23:45:67:89:ab\nordinary observation\n");
        check(clean.find("secret")==clean.npos && clean.find("123")==clean.npos && clean.find("ordinary observation")!=clean.npos,"Report redaction failed");
        write(work/"bad-gpt",std::string(4096*6,'\0')); ure::Fd disk(::open((work/"bad-gpt").c_str(),O_RDONLY));
        check(ure::gpt_inspect(disk.get(),4096)["state"]=="NO_VALID_GPT","Invalid GPT was accepted");
        check(ure::filesystem_probe(disk.get())["type"]=="unknown","Unknown filesystem was guessed");
        ure::fs::create_directories(tree/"Windows/System32/config"); write(tree/"Windows/System32/config/SYSTEM","hive");
        check(ure::windows_detect(root)["detected"].asBool(),"Windows detection failed");
        std::cout<<"URE host tests: discovery, negative parsers, symlinks, metadata, transactions, rollback and redaction passed\n";
        ure::fs::remove_all(work); return 0;
    } catch(const std::exception& e) { std::cerr<<"URE test failure: "<<e.what()<<'\n'; ure::fs::remove_all(work); return 1; }
}
