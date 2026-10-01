// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <algorithm>
#include <array>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

namespace {
constexpr std::uint64_t mib=1048576,capacity=4096*mib;
void check(bool value,const std::string& message) { if(!value)throw std::runtime_error(message); }
template<class F> void reject(F action,const std::string& code) {
    try { action(); } catch(const ure::Error& e) { check(e.code==code,"Unexpected refusal "+e.code+", wanted "+code); return; } throw std::runtime_error("Expected refusal "+code);
}
void put(std::string& bytes,std::size_t offset,std::uint64_t value,unsigned count) {
    for(unsigned i=0;i<count;++i)bytes.at(offset+i)=static_cast<char>((value>>(i*8))&255);
}
std::uint32_t crc(std::string_view data) {
    std::uint32_t out=UINT32_MAX; for(char byte:data) { out^=static_cast<unsigned char>(byte); for(unsigned i=0;i<8;++i)out=(out>>1)^((out&1U) ? 0xedb88320U : 0U); } return out^UINT32_MAX;
}
void write(int fd,const std::string& bytes,std::uint64_t offset) { check(::pwrite(fd,bytes.data(),bytes.size(),static_cast<off_t>(offset))==static_cast<ssize_t>(bytes.size()),"Fixture write failed"); }
void fixture(const ure::fs::path& file,unsigned sector,bool obstacle=false,bool attributed=false) {
    ure::Fd fd(::open(file.c_str(),O_RDWR|O_CREAT|O_TRUNC,0600)); check(fd.get()>=0 && ::ftruncate(fd.get(),static_cast<off_t>(capacity))==0,"Fixture creation failed");
    const auto sectors=capacity/sector; constexpr unsigned count=96,table_size=count*128; const auto table_sectors=16384/sector;
    const auto first=2+table_sectors; const auto last=sectors-2-table_sectors,backup=last+1; std::string entries(table_size,'\0');
    auto entry=[&](unsigned slot,std::uint64_t begin,std::uint64_t end,const std::string& name,bool reserved=false) {
        const auto offset=slot*128;
        if(!reserved)for(unsigned i=0;i<16;++i)entries[offset+i]=static_cast<char>(i+11);
        for(unsigned i=0;i<16;++i)entries[offset+16+i]=static_cast<char>(i+51);
        put(entries,offset+16,slot+1,4); put(entries,offset+32,begin,8); put(entries,offset+40,end,8);
        put(entries,offset+48,reserved ? 1ULL<<60 : attributed && name=="userdata" ? 1 : 0,8);
        for(std::size_t i=0;i<name.size();++i)entries[offset+56+i*2]=name[i];
    };
    entry(0,mib/sector,16*mib/sector-1,"super"); entry(1,16*mib/sector,(capacity-mib)/sector-1,"userdata");
    entry(2,(capacity-mib)/sector,last,sector==4096 ? "last_parti" : "oem_reserved",sector==4096);
    if(obstacle) { entry(1,16*mib/sector,32*mib/sector-1,"userdata"); entry(3,32*mib/sector,48*mib/sector-1,"unknown_oem");
        entry(4,48*mib/sector,(capacity-mib)/sector-1,"uke_linux");
        // Linux data type GUID in GPT byte order.
        const std::array<unsigned char,16> type{0xaf,0x3d,0xc6,0x0f,0x83,0x84,0x72,0x47,0x8e,0x79,0x3d,0x69,0xd8,0x47,0x7d,0xe4};
        for(unsigned i=0;i<type.size();++i)entries[4*128+i]=static_cast<char>(type[i]); }
    write(fd.get(),entries,2*sector); write(fd.get(),entries,backup*sector);
    for(const auto lba:std::array<std::uint64_t,2>{1,sectors-1}) {
        std::string header(sector,'\0'); header.replace(0,8,"EFI PART"); put(header,8,0x10000,4); put(header,12,92,4);
        put(header,24,lba,8); put(header,32,sectors-lba,8); put(header,40,first,8); put(header,48,last,8);
        for(unsigned i=0;i<16;++i)header[56+i]=static_cast<char>(i+81);
        put(header,72,lba==1 ? 2 : backup,8); put(header,80,count,4); put(header,84,128,4); put(header,88,crc(entries),4);
        put(header,16,crc(std::string_view(header).substr(0,92)),4); write(fd.get(),header,lba*sector);
    }
    std::string mbr(sector,'\0'); mbr[450]=static_cast<char>(0xee); put(mbr,454,1,4); put(mbr,458,sectors-1,4); mbr[510]=0x55; mbr[511]=static_cast<char>(0xaa); write(fd.get(),mbr,0);
    write(fd.get(),"protected-super-data",2*mib); write(fd.get(),"existing-user-data",17*mib); check(::fsync(fd.get())==0,"Cannot sync fixture");
}
ure::Value request() {
    ure::Value out; out["schema"]=1; out["format"]="ure-layout-request"; out["rows"]=ure::Value(Json::arrayValue);
    const std::array<std::string,4> roles{"esp","linux","windows","userdata"},sizes{"128","40","20",""},units{"MiB","%","%","remaining"},filesystems{"fat32","ext4","ntfs","f2fs"};
    for(unsigned i=0;i<4;++i) { ure::Value row; row["role"]=roles[i]; row["size"]=sizes[i]; row["unit"]=units[i]; row["filesystem"]=filesystems[i]; out["rows"].append(row); } return out;
}
struct Workspace {
    ure::fs::path path;
    Workspace() { std::array<char,64> pattern{}; const std::string text="/tmp/ure-layout-tests-XXXXXX"; std::copy(text.begin(),text.end(),pattern.begin()); const auto* name=::mkdtemp(pattern.data()); check(name,"mkdtemp failed"); path=name; }
    ~Workspace() { std::error_code error; ure::fs::remove_all(path,error); }
};
}
int main(int argc,char* argv[]) {
    try {
        if(argc==3 && std::string_view(argv[1])=="--fixture") { fixture(argv[2],4096); return 0; }
        if(argc==2 && std::string_view(argv[1])=="--request") { std::cout<<ure::json(request()); return 0; }
        check(ure::layout_size_bytes("1","GB",capacity)==1000000000 && ure::layout_size_bytes("1","GiB",capacity)==1073741824 &&
            ure::layout_size_bytes("1.5","MiB",capacity)==1572864 && ure::layout_size_bytes("1,5","MiB",capacity)==1572864,"Decimal and binary units differ");
        check(ure::layout_size_bytes("0.000001","%",INT64_MAX)==92233720368ULL && ure::layout_size_bytes("100","%",INT64_MAX)==INT64_MAX,"Percentage loses integer precision");
        for(const auto* bad:{"-1","+1","1e2"," 1","1 ",".5","1.","1.2345678","1,2.3",""})reject([&] { ure::layout_size_bytes(bad,"GB",capacity); },"invalid-layout-size");
        reject([&] { ure::layout_size_bytes("999999999999999999999999","GB",capacity); },"layout-size-overflow");
        reject([&] { ure::layout_size_bytes("100.000001","%",capacity); },"invalid-layout-percent");
        reject([&] { ure::layout_size_bytes("1","MB",capacity); },"invalid-layout-unit"); Workspace work;
        for(unsigned sector:{512U,4096U}) {
            const auto file=work.path/(std::to_string(sector)+".img"); fixture(file,sector); auto target=ure::storage_image(file,sector);
            const auto original=ure::gpt_inspect(target.descriptor.get(),sector); check(original["healthy"]==true,"Bad GPT fixture");
            const auto layout=ure::partition_layout(target,request(),"global-os3.0.303.0"); check(layout["pool"]["bytes"]==Json::UInt64(4079*mib) && layout["protected_records"].size()==2,"User pool includes protected bytes");
            check(layout["rows"][0]["index"].asUInt()==2 && layout["rows"][0]["partuuid"]==original["partitions"][1]["partuuid"] && layout["rows"][0]["start_lba"]==original["partitions"][1]["start_lba"],"Existing userdata start, index or GUID changed");
            check(layout["rows"][2]["bytes"]==Json::UInt64(1631*mib) && layout["rows"][3]["bytes"]==Json::UInt64(815*mib) && layout["unallocated_bytes"].asUInt64()==0,"Percentage basis or alignment is wrong");
            for(const auto& row:layout["rows"])if(row["enabled"]==true)check(row["start_lba"].asUInt64()>=original["partitions"][1]["start_lba"].asUInt64() && row["end_lba"].asUInt64()<=original["partitions"][1]["end_lba"].asUInt64(),"New partition escaped original userdata");
            check(layout["complete_partition_job"]==false && layout["formats_filesystems"]==false && layout["live_write_backend_ready"]==false,"Planner overstates execution");
            const auto repeat=ure::partition_layout(target,layout["request"],"global-os3.0.303.0"); check(ure::json(repeat)==ure::json(layout),"Resolved layout is nondeterministic");
            for(unsigned width:{1U,17U,3000U,32768U}) { const auto graph=ure::partition_layout_bar(layout,width); unsigned end=0;
                for(const auto& piece:graph) { check(piece["x"].asUInt()==end,"Graph has a gap or overlap"); end+=piece["width"].asUInt(); } check(end==width,"Graph escaped width"); }
            check(ure::partition_layout_text(layout).find("userdata / f2fs")!=std::string::npos,"Readable review is missing");
            auto bad=request(); bad["rows"][1]["size"]="90"; reject([&] { ure::partition_layout(target,bad,"global-os3.0.303.0"); },"insufficient-layout-space");
            bad=request(); bad["rows"][0]["filesystem"]="ext4"; reject([&] { ure::partition_layout(target,bad,"global-os3.0.303.0"); },"invalid-layout-filesystem");
            bad=request(); bad["rows"][0]["role"]="super"; reject([&] { ure::partition_layout(target,bad,"global-os3.0.303.0"); },"invalid-layout-role");
            bad=request(); bad["rows"][0]["size"]="0.1"; reject([&] { ure::partition_layout(target,bad,"global-os3.0.303.0"); },"layout-size-too-small");
            bad=request(); bad["rows"][1]["unit"]="remaining"; bad["rows"][1]["size"]=""; reject([&] { ure::partition_layout(target,bad,"global-os3.0.303.0"); },"invalid-layout-remainder");
            const auto plan=ure::gpt_layout_plan(target,layout["request"],"global-os3.0.303.0"); check(plan["desired_table"]["healthy"]==true && plan["desired_table"]["partitions"].size()==(sector==4096 ? 5 : 6),"Proposed GPT is invalid");
            check(ure::json(plan["desired_table"]["reserved_records"])==ure::json(original["reserved_records"]) && ure::json(plan["desired_table"]["partitions"][0])==ure::json(original["partitions"][0]),"Protected GPT record changed");
            auto writable=ure::storage_image(file,sector,true); const auto journal=work.path/("journal-"+std::to_string(sector));
            reject([&] { ure::gpt_execute(writable,plan,journal,"wrong"); },"confirmation-required"); check(!ure::fs::exists(journal),"Wrong confirmation created a journal");
            const auto applied=ure::gpt_execute(writable,plan,journal,plan["plan_sha256"].asString()); check(applied["state"]=="COMMITTED" && applied["execution_scope"]=="GPT_METADATA_ONLY" && applied["complete_partition_job"]==false,"Metadata scope is missing");
            check(ure::storage_read(writable.descriptor.get(),2*mib,20)=="protected-super-data" && ure::storage_read(writable.descriptor.get(),17*mib,18)=="existing-user-data","Metadata edit modified payload bytes");
            writable=ure::storage_image(file,sector,true); const auto review=ure::gpt_journal_inspect(writable,journal);
            check(review["classification"]=="TARGET_CONTENT_VERIFIED" && review["recovery_actions"].size()==1 && review["recovery_actions"][0]=="rollback","Verified rollback is unavailable");
            check(ure::gpt_rollback(writable,journal,plan["plan_sha256"].asString())["state"]=="ROLLED_BACK","Rollback failed");
            check(ure::json(ure::gpt_inspect(writable.descriptor.get(),sector))==ure::json(original),"Rollback did not restore exact table");
            target=ure::storage_image(file,sector);
            bad=request(); bad["placement"]="before_userdata"; reject([&] { ure::partition_layout(target,bad,"global-os3.0.303.0"); },"userdata-migration-required");
            bad["userdata_policy"]="recreate"; reject([&] { ure::partition_layout(target,bad,"global-os3.0.303.0"); },"advanced-mode-required"); bad["mode"]="advanced";
            const auto front=ure::gpt_layout_plan(target,bad,"global-os3.0.303.0");
            check(front["layout"]["rows"][3]["action"]=="ERASE_AND_RECREATE_REQUIRED" && front["layout"]["rows"][3]["destroys_existing_data"]==true && front["layout"]["rows"][3]["start_lba"].asUInt64()>original["partitions"][1]["start_lba"].asUInt64(),"Advanced front placement concealed userdata loss");
            check(ure::json(front["desired_table"]["partitions"][0])==ure::json(original["partitions"][0]),"Front placement modified super");
            bad=request(); bad["rows"][3]["partuuid"]="11111111-2222-4333-8444-555555555555";
            reject([&] { ure::partition_layout(target,bad,"global-os3.0.303.0"); },"advanced-mode-required"); bad["mode"]="advanced";
            check(ure::partition_layout(target,bad,"global-os3.0.303.0")["rows"][0]["identity_changed"]==true,"Advanced userdata GUID change was ignored");
            bad=request(); ure::Value change; change["index"]=1; change["partuuid"]="11111111-2222-4333-8444-555555555555"; change["contents"]="format"; change["filesystem"]="ext4"; bad["record_edits"].append(change);
            reject([&] { ure::partition_layout(target,bad,"global-os3.0.303.0"); },"advanced-mode-required"); bad["mode"]="advanced";
            const auto expert=ure::gpt_layout_plan(target,bad,"global-os3.0.303.0");
            check(expert["desired_table"]["partitions"][0]["partuuid"]==change["partuuid"] && expert["layout"]["advanced_record_edits"][0]["destroys_existing_data"]==true && expert["formats_filesystems"]==false,"Advanced requested edits or metadata scope are wrong");
            check(expert["desired_table"]["partitions"][0]["start_lba"]==original["partitions"][0]["start_lba"] && expert["desired_table"]["partitions"][0]["end_lba"]==original["partitions"][0]["end_lba"],"Advanced GUID edit changed geometry");
            bad["record_edits"][0]["partuuid"]=original["partitions"][1]["partuuid"]; reject([&] { ure::partition_layout(target,bad,"global-os3.0.303.0"); },"invalid-layout-guid");
            fixture(file,sector,true); target=ure::storage_image(file,sector); reject([&] { ure::partition_layout(target,request(),"global-os3.0.303.0"); },"insufficient-layout-space");
            bad=request(); for(unsigned i=0;i<3;++i) { bad["rows"][i]["size"]="0"; bad["rows"][i]["unit"]="MiB"; }
            const auto constrained=ure::gpt_layout_plan(target,bad,"global-os3.0.303.0"); check(constrained["layout"]["pool"]["bytes"]==Json::UInt64(16*mib) && ure::json(constrained["desired_table"])==ure::json(constrained["current_table"]),"Other OS partitions or free gaps were allocated");
            bad["rows"][1]["size"]="1"; reject([&] { ure::partition_layout(target,bad,"global-os3.0.303.0"); },"existing-os-partition");
            fixture(file,sector,false,true); target=ure::storage_image(file,sector); reject([&] { ure::partition_layout(target,request(),"global-os3.0.303.0"); },"protected-partition");
        }
        std::cout<<"Layout: exact GB/GiB/MiB/percent math, alignment, protected GPT records, role/filesystem rules, graph widths, deterministic plans, scoped image metadata execution/readback and rollback passed; synthetic images only.\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
