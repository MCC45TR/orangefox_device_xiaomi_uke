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
void fixture_write(int fd,const std::string& bytes,std::uint64_t offset) { check(::pwrite(fd,bytes.data(),bytes.size(),static_cast<off_t>(offset))==static_cast<ssize_t>(bytes.size()),"Fixture write failed"); }
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
    fixture_write(fd.get(),entries,2*sector); fixture_write(fd.get(),entries,backup*sector);
    for(const auto lba:std::array<std::uint64_t,2>{1,sectors-1}) {
        std::string header(sector,'\0'); header.replace(0,8,"EFI PART"); put(header,8,0x10000,4); put(header,12,92,4);
        put(header,24,lba,8); put(header,32,sectors-lba,8); put(header,40,first,8); put(header,48,last,8);
        for(unsigned i=0;i<16;++i)header[56+i]=static_cast<char>(i+81);
        put(header,72,lba==1 ? 2 : backup,8); put(header,80,count,4); put(header,84,128,4); put(header,88,crc(entries),4);
        put(header,16,crc(std::string_view(header).substr(0,92)),4); fixture_write(fd.get(),header,lba*sector);
    }
    std::string mbr(sector,'\0'); mbr[450]=static_cast<char>(0xee); put(mbr,454,1,4); put(mbr,458,sectors-1,4); mbr[510]=0x55; mbr[511]=static_cast<char>(0xaa); fixture_write(fd.get(),mbr,0);
    fixture_write(fd.get(),"protected-super-data",2*mib); fixture_write(fd.get(),"existing-user-data",17*mib); check(::fsync(fd.get())==0,"Cannot sync fixture");
}
// Uke-shaped capacity fixture: all 32 declared entries are occupied, with
// userdata last, 4 KiB sectors, and a pre-existing 16 KiB table reservation.
// The zero reserve here is synthetic; copied captures that omitted it cannot
// establish the actual tablet's reserve contents.
void full_table_fixture(const ure::fs::path& file,unsigned sector=4096,bool saturated=true,bool tight=false) {
    ure::Fd fd(::open(file.c_str(),O_RDWR|O_CREAT|O_TRUNC,0600)); check(fd.get()>=0 && ::ftruncate(fd.get(),static_cast<off_t>(capacity))==0,"Cannot create full-table fixture");
    const auto sectors=capacity/sector; constexpr unsigned count=32,table_size=count*128;
    const std::uint64_t reserve_sectors=(tight ? table_size : 16384)/sector,first=2+reserve_sectors,last=sectors-2-reserve_sectors,backup=last+1;
    std::string entries(table_size,'\0');
    for(unsigned index=0;index<count;++index) {
        const auto offset=index*128; for(unsigned byte=0;byte<16;++byte) { entries[offset+byte]=static_cast<char>(byte+11); entries[offset+16+byte]=static_cast<char>(byte+51); }
        put(entries,offset+16,index+1,4); const auto begin=(index+1)*mib/sector,end=index==31 ? last : (index+2)*mib/sector-1;
        put(entries,offset+32,begin,8); put(entries,offset+40,end,8); const auto name=index==31 ? "userdata" : "oem_"+std::to_string(index+1);
        for(std::size_t i=0;i<name.size();++i)entries[offset+56+i*2]=name[i];
    }
    fixture_write(fd.get(),entries,2*sector); fixture_write(fd.get(),entries,backup*sector);
    if(!tight) {
        // Distinct undeclared padding beyond the new 64-entry prefix must
        // survive independently in each existing table reservation.
        fixture_write(fd.get(),"primary-reserve-tail",2*sector+96*128);
        fixture_write(fd.get(),"backup-reserve-tail",backup*sector+96*128);
    }
    for(const auto lba:std::array<std::uint64_t,2>{1,sectors-1}) {
        std::string header(sector,'\0'); header.replace(0,8,"EFI PART"); put(header,8,0x10000,4); put(header,12,92,4); put(header,24,lba,8); put(header,32,sectors-lba,8);
        put(header,40,first,8); put(header,48,last,8); for(unsigned byte=0;byte<16;++byte)header[56+byte]=static_cast<char>(byte+81);
        put(header,72,lba==1 ? 2 : backup,8); put(header,80,count,4); put(header,84,128,4); put(header,88,crc(entries),4); put(header,16,crc(std::string_view(header).substr(0,92)),4); fixture_write(fd.get(),header,lba*sector);
    }
    std::string mbr(sector,'\0'); mbr[450]=static_cast<char>(0xee); put(mbr,454,1,4); put(mbr,458,saturated ? UINT32_MAX : sectors-1,4); mbr[510]=0x55; mbr[511]=static_cast<char>(0xaa); fixture_write(fd.get(),mbr,0);
    for(unsigned index=1;index<32;++index)fixture_write(fd.get(),"protected_"+std::to_string(index),index*mib);
    check(::fsync(fd.get())==0,"Cannot sync full-table fixture");
}
ure::Value request() {
    ure::Value out; out["schema"]=1; out["format"]="ure-layout-request"; out["rows"]=ure::Value(Json::arrayValue);
    const std::array<std::string,4> roles{"esp","linux","windows","userdata"},sizes{"128","40","20",""},units{"MiB","%","%","remaining"},filesystems{"fat32","ext4","ntfs","f2fs"};
    for(unsigned i=0;i<4;++i) { ure::Value row; row["role"]=roles[i]; row["size"]=sizes[i]; row["unit"]=units[i]; row["filesystem"]=filesystems[i]; out["rows"].append(row); } return out;
}
ure::Value boot_request() {
    auto out=request(); ure::Value row; row["role"]="linux_boot"; row["size"]="256"; row["unit"]="MiB"; row["filesystem"]="ext4"; out["rows"].append(row); return out;
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
        check(ure::layout_size_bytes("1.5","MB",capacity)==1500000,"Decimal MB is wrong");
        reject([&] { ure::layout_size_bytes("1","KB",capacity); },"invalid-layout-unit"); Workspace work;
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
        const auto full=work.path/"full-uke-table.img"; full_table_fixture(full);
        auto selected=ure::storage_image(full,4096); const auto full_before=ure::gpt_inspect(selected.descriptor.get(),4096);
        const auto complete_before=ure::sha256(selected.descriptor.get());
        check(full_before["healthy"]==true && full_before["protective_mbr_classification"]=="OEM_SATURATED_4K" && full_before["partitions"].size()==32,"Valid Uke-shaped saturated protective MBR was rejected");
        auto choices=boot_request(); choices["mode"]="advanced"; choices["userdata_policy"]="recreate";
        const auto expanded=ure::gpt_layout_plan(selected,choices,"fixture-uke");
        check(expanded["layout"]["rows"].size()==5 && expanded["layout"]["rows"][0]["role"]=="userdata" && expanded["layout"]["rows"][1]["role"]=="esp" &&
            expanded["layout"]["rows"][2]["role"]=="linux_boot" && expanded["layout"]["rows"][3]["role"]=="linux" && expanded["layout"]["rows"][4]["role"]=="windows","Separate Linux boot is out of order");
        check(expanded["layout"]["gpt_entry_table"]["original_count"].asUInt()==32 && expanded["layout"]["gpt_entry_table"]["proposed_count"].asUInt()==64 &&
            expanded["layout"]["gpt_entry_table"]["extension_zero_verified_both_copies"]==true && expanded["desired_table"]["healthy"]==true,"Bounded GPT declaration expansion failed");
        for(const auto* copy:{"primary","backup"})for(const auto* field:{"table_lba","current_lba","alternate_lba","first_usable_lba","last_usable_lba"})
            check(expanded["desired_table"][copy][field]==full_before[copy][field],"GPT declaration expansion moved metadata or usable boundaries");
        for(Json::ArrayIndex i=0;i<31;++i)check(ure::json(expanded["desired_table"]["partitions"][i])==ure::json(full_before["partitions"][i]),"Expansion changed protected OEM records");
        ure::Value resolved; const auto proposed=ure::gpt_layout_regions(selected,expanded["layout"]["request"],"fixture-uke",resolved);
        for(const auto& range:proposed) {
            const auto before=ure::storage_read(selected.descriptor.get(),range.offset,range.bytes.size());
            if(range.name=="protective_mbr")check(range.bytes==before,"OEM protective MBR was normalized");
            if(range.name=="primary_table" || range.name=="backup_table") {
                check(range.bytes.substr(0,31*128)==before.substr(0,31*128),"Unselected record bytes changed");
                check(range.bytes.substr(64*128)==before.substr(64*128),"Undeclared reserve tail changed");
            }
        }
        const auto repeat=ure::partition_layout(selected,expanded["layout"]["request"],"fixture-uke"); check(ure::json(repeat)==ure::json(expanded["layout"]),"Expanded resolved layout is nondeterministic");
        for(unsigned width:{1U,17U,3000U}) { unsigned end=0; for(const auto& row:ure::partition_layout_bar(repeat,width)) { check(row["x"].asUInt()==end,"Five-row graph has a gap"); end+=row["width"].asUInt(); } check(end==width,"Five-row graph exceeds width"); }
        const auto legacy_expanded=ure::gpt_layout_plan(selected,request(),"fixture-uke"); check(legacy_expanded["layout"]["rows"].size()==4 && legacy_expanded["desired_table"]["primary"]["entry_count"].asUInt()==64,"Four-role caller no longer expands safely");
        const auto expansion_journal=work.path/"expanded-journal"; const auto expanded_hash=expanded["plan_sha256"].asString();
        auto expanded_writer=ure::storage_image(full,4096,true);
        check(ure::gpt_execute(expanded_writer,expanded,expansion_journal,expanded_hash)["state"]=="COMMITTED","Full32-to64 GPT metadata application failed");
        for(const auto& range:proposed)check(ure::storage_read(expanded_writer.descriptor.get(),range.offset,range.bytes.size())==range.bytes,"Applied expansion differs from exact reviewed metadata");
        for(unsigned index=1;index<32;++index)check(ure::storage_read(expanded_writer.descriptor.get(),index*mib,("protected_"+std::to_string(index)).size())=="protected_"+std::to_string(index),"Expansion changed an OEM payload marker");
        // Simulate a forced restart after only the backup count/table reached
        // their target and during the primary header CRC/count update. The
        // journal oracle must classify exact old/new byte mixtures and permit
        // only safe recovery, without trusting a progress counter.
        ure::Root expansion_store(expansion_journal);
        for(const auto& range:proposed)if(range.name=="primary_table" || range.name=="primary_header")
            fixture_write(expanded_writer.descriptor.get(),expansion_store.read("before-"+range.name+".bin"),range.offset);
        for(const auto& range:proposed)if(range.name=="primary_header")fixture_write(expanded_writer.descriptor.get(),range.bytes.substr(0,18),range.offset);
        check(::fsync(expanded_writer.descriptor.get())==0,"Cannot synchronize partial expansion fixture");
        auto uncertain=ure::json_file(expansion_journal/"journal.json"); uncertain["state"]="EXECUTING"; uncertain["completed_ranges"].append("untrusted-progress"); expansion_store.save_record("journal.json",uncertain,true);
        expanded_writer=ure::storage_image(full,4096,true);
        const auto partial=ure::gpt_journal_inspect(expanded_writer,expansion_journal);
        check(partial["classification"]=="PARTIAL_EXPECTED_WRITE" && partial["current_table"]["healthy"]==false,"Torn32-to64 expansion was mistaken for a healthy committed table");
        reject([&] { ure::gpt_resume(expanded_writer,expansion_journal,expanded_hash); },"unsafe-resume");
        check(ure::gpt_rollback(expanded_writer,expansion_journal,expanded_hash)["state"]=="ROLLED_BACK","Partial expansion did not roll back");
        check(ure::sha256(expanded_writer.descriptor.get())==complete_before,"Expansion rollback did not restore every byte of the complete synthetic disk");
        selected=ure::storage_image(full,4096);
        auto invalid_boot=boot_request(); invalid_boot["rows"][1]["size"]="0";
        reject([&] { ure::partition_layout(selected,invalid_boot,"fixture-uke"); },"linux-boot-without-root");
        invalid_boot=boot_request(); invalid_boot["rows"][4]["filesystem"]="btrfs";
        reject([&] { ure::partition_layout(selected,invalid_boot,"fixture-uke"); },"invalid-layout-filesystem");
        invalid_boot=request(); invalid_boot["rows"][1]=boot_request()["rows"][4];
        reject([&] { ure::partition_layout(selected,invalid_boot,"fixture-uke"); },"invalid-layout-request");
        const auto backup_lba=full_before["backup"]["table_lba"].asUInt64();
        for(const auto offset:std::array<std::uint64_t,2>{2ULL*4096+32*128,backup_lba*4096+32*128}) {
            { auto writer=ure::storage_image(full,4096,true); fixture_write(writer.descriptor.get(),std::string(1,'x'),offset); check(::fsync(writer.descriptor.get())==0,"Cannot dirty extension slot"); }
            auto dirty=ure::storage_image(full,4096); const auto observed=ure::gpt_inspect(dirty.descriptor.get(),4096);
            check(observed["healthy"]==true,"Undeclared reserve mutation unexpectedly changed the declared GPT: "+ure::json(observed));
            reject([&] { ure::partition_layout(dirty,choices,"fixture-uke"); },"layout-table-reserve-not-empty");
            { auto writer=ure::storage_image(full,4096,true); fixture_write(writer.descriptor.get(),std::string(1,'\0'),offset); check(::fsync(writer.descriptor.get())==0,"Cannot restore extension slot"); }
        }
        // A saturated length cannot turn a damaged, conflicting, hybrid or
        // 512-byte-sector table into an accepted source.
        { auto writer=ure::storage_image(full,4096,true); const auto header=ure::storage_read(writer.descriptor.get(),4096,4096); auto corrupt=header; corrupt[16]^=1; fixture_write(writer.descriptor.get(),corrupt,4096); check(::fsync(writer.descriptor.get())==0,"Cannot corrupt header");
          check(ure::gpt_inspect(writer.descriptor.get(),4096)["protective_mbr_valid"]==false,"Saturated MBR accepted with one invalid GPT copy"); fixture_write(writer.descriptor.get(),header,4096); }
        { auto writer=ure::storage_image(full,4096,true); auto mbr=ure::storage_read(writer.descriptor.get(),0,4096); auto hybrid=mbr; hybrid[466]=static_cast<char>(0x83); fixture_write(writer.descriptor.get(),hybrid,0);
          check(ure::gpt_inspect(writer.descriptor.get(),4096)["healthy"]==false,"Hybrid MBR accepted as OEM saturated");
          auto wrong_start=mbr; put(wrong_start,454,2,4); fixture_write(writer.descriptor.get(),wrong_start,0); check(ure::gpt_inspect(writer.descriptor.get(),4096)["healthy"]==false,"Wrong protective start accepted");
          auto bootable=mbr; bootable[446]=static_cast<char>(0x80); fixture_write(writer.descriptor.get(),bootable,0); check(ure::gpt_inspect(writer.descriptor.get(),4096)["healthy"]==false,"Bootable protective entry accepted");
          auto bad_signature=mbr; bad_signature[510]=0; fixture_write(writer.descriptor.get(),bad_signature,0); check(ure::gpt_inspect(writer.descriptor.get(),4096)["healthy"]==false,"Invalid MBR signature accepted"); fixture_write(writer.descriptor.get(),mbr,0); }
        { auto writer=ure::storage_image(full,4096,true); const auto offset=capacity-4096; const auto header=ure::storage_read(writer.descriptor.get(),offset,4096); auto conflict=header; conflict[56]^=1; put(conflict,16,0,4); put(conflict,16,crc(std::string_view(conflict).substr(0,92)),4);
          fixture_write(writer.descriptor.get(),conflict,offset); const auto mismatched=ure::gpt_inspect(writer.descriptor.get(),4096);
          check(mismatched["primary"]["valid"]==true && mismatched["backup"]["valid"]==true && mismatched["protective_mbr_valid"]==false && mismatched["healthy"]==false,"Saturated exception accepted conflicting valid disk identities"); fixture_write(writer.descriptor.get(),header,offset); }
        const auto narrow=work.path/"narrow-gpt.img"; full_table_fixture(narrow,4096,true,true); auto tight=ure::storage_image(narrow,4096);
        reject([&] { ure::partition_layout(tight,choices,"fixture-uke"); },"unhealthy-layout-source");
        const auto bad_sector=work.path/"saturated-512.img"; full_table_fixture(bad_sector,512,true); auto sector512=ure::storage_image(bad_sector,512);
        check(ure::gpt_inspect(sector512.descriptor.get(),512)["protective_mbr_valid"]==false,"OEM saturated exception leaked to 512-byte media");
        std::cout<<"Layout: MB/MiB/GB/GiB/percent math, optional Linux boot, bounded full-table expansion, saturated 4K OEM MBR classification/refusals, protected record bytes, graph widths, deterministic plans and image metadata rollback passed; synthetic images only.\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
