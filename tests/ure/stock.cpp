// SPDX-License-Identifier: Apache-2.0
// Host-only independent XML patch oracle; never executes an OEM program.
#include "uke.h"
#include <algorithm>
#include <array>
#include <fcntl.h>
#include <iostream>
#include <libxml/parser.h>
#include <map>
#include <unistd.h>

namespace {
void check(bool condition,const std::string& message) { if(!condition)throw std::runtime_error(message); }
template<class F> void reject(F action,const std::string& code) {
    try { action(); } catch(const ure::Error& error) { check(error.code==code,"Unexpected rejection "+error.code+", wanted "+code); return; }
    throw std::runtime_error("Expected refusal "+code);
}
std::uint64_t le(std::string_view bytes,std::size_t offset,unsigned count) {
    std::uint64_t value=0; for(unsigned i=0;i<count;++i)value|=static_cast<std::uint64_t>(static_cast<unsigned char>(bytes.at(offset+i)))<<(8*i); return value;
}
void put(std::string& bytes,std::size_t offset,std::uint64_t value,unsigned count) {
    for(unsigned i=0;i<count;++i)bytes.at(offset+i)=static_cast<char>((value>>(8*i))&255);
}
std::uint32_t crc(std::string_view bytes) {
    std::uint32_t value=UINT32_MAX;
    for(const char c:bytes) { value^=static_cast<unsigned char>(c); for(unsigned i=0;i<8;++i)value=(value>>1)^((value&1U) ? 0xedb88320U : 0U); }
    return value^UINT32_MAX;
}
std::uint64_t number(std::string text,std::uint64_t sectors) {
    if(text.ends_with('.'))text.pop_back();
    if(text.starts_with("NUM_DISK_SECTORS-"))return sectors-std::stoull(text.substr(17));
    return std::stoull(text);
}
std::string property(xmlNode* node,const char* name) {
    auto* raw=xmlGetProp(node,reinterpret_cast<const xmlChar*>(name)); check(raw!=nullptr,"Missing oracle patch attribute");
    std::string value(reinterpret_cast<const char*>(raw)); xmlFree(raw); return value;
}
std::map<std::string,std::string> oracle(const ure::fs::path& inputs,unsigned lun,std::uint64_t capacity) {
    const auto suffix=std::to_string(lun)+".bin"; std::map<std::string,std::string> files;
    for(const auto* prefix:{"gpt_main","gpt_backup"})files.emplace(std::string(prefix)+suffix,ure::bounded_read(inputs/(std::string(prefix)+suffix),65536));
    const auto text=ure::bounded_read(inputs/("patch"+std::to_string(lun)+".xml"),65536);
    auto* document=xmlReadMemory(text.data(),static_cast<int>(text.size()),"patch.xml",nullptr,XML_PARSE_NONET);
    check(document!=nullptr && document->intSubset==nullptr && document->extSubset==nullptr,"Invalid oracle XML");
    try {
        for(auto* node=xmlDocGetRootElement(document)->children;node;node=node->next) {
            if(node->type!=XML_ELEMENT_NODE)continue;
            const auto name=property(node,"filename"); if(name=="DISK")continue;
            check(number(property(node,"SECTOR_SIZE_IN_BYTES"),0)==4096 && number(property(node,"physical_partition_number"),0)==lun,"Wrong oracle geometry");
            auto& file=files.at(name); const auto start=number(property(node,"start_sector"),capacity/4096);
            const auto offset=start*4096+number(property(node,"byte_offset"),0),size=number(property(node,"size_in_bytes"),0);
            const auto expression=property(node,"value"); std::uint64_t value=0;
            if(expression.starts_with("CRC32(")) {
                const auto comma=expression.find(','); check(comma!=expression.npos && expression.ends_with(')'),"Invalid CRC oracle expression");
                const auto begin=number(expression.substr(6,comma-6),capacity/4096)*4096;
                const auto bytes=number(expression.substr(comma+1,expression.size()-comma-2),capacity/4096);
                check(begin<=file.size() && bytes<=file.size()-begin,"CRC oracle range is invalid");
                value=crc(std::string_view(file).substr(static_cast<std::size_t>(begin),static_cast<std::size_t>(bytes)));
            } else value=number(expression,capacity/4096);
            check(offset<=file.size() && (size==4 || size==8) && size<=file.size()-offset,"Oracle patch is out of range");
            put(file,static_cast<std::size_t>(offset),value,static_cast<unsigned>(size));
        }
    } catch(...) { xmlFreeDoc(document); throw; }
    xmlFreeDoc(document);
    put(files.at("gpt_main"+suffix),458,std::min<std::uint64_t>(capacity/4096-1,UINT32_MAX),4);
    return files;
}
void write(int fd,std::uint64_t offset,std::string_view bytes) {
    check(::pwrite(fd,bytes.data(),bytes.size(),static_cast<off_t>(offset))==static_cast<ssize_t>(bytes.size()),"Fixture write failed");
    check(::fsync(fd)==0,"Fixture sync failed");
}
void save(const ure::fs::path& image,std::uint64_t capacity,const std::vector<ure::StorageRange>& ranges) {
    ure::Fd file(::open(image.c_str(),O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC,0600)); check(file.get()>=0,"Cannot create sparse fixture");
    check(::ftruncate(file.get(),static_cast<off_t>(capacity))==0,"Cannot size sparse fixture");
    for(const auto& range:ranges)write(file.get(),range.offset,range.bytes);
}
void update_tables(int fd,std::uint64_t capacity,std::string entries,unsigned count) {
    for(const auto position:std::array<std::uint64_t,2>{4096,capacity-4096}) {
        auto header=ure::storage_read(fd,position,4096); put(header,80,count,4); put(header,88,crc(std::string_view(entries).substr(0,count*128)),4);
        put(header,16,0,4); put(header,16,crc(std::string_view(header).substr(0,92)),4); write(fd,position,header);
    }
    write(fd,8192,entries); write(fd,capacity-20480,entries);
}
}
int main(int argc,char* argv[]) {
    if(argc!=2) { std::cerr<<"Provide the pinned Global stock input directory\n"; return 1; }
    std::array<char,40> pattern{}; const std::string text="/tmp/ure-stock-tests-XXXXXX"; std::copy(text.begin(),text.end(),pattern.begin());
    const auto* created=::mkdtemp(pattern.data()); if(!created)return 1; const ure::fs::path work(created),inputs(argv[1]);
    constexpr std::array<std::uint64_t,6> base{32ULL<<30,16ULL<<20,16ULL<<20,4ULL<<20,2ULL<<30,128ULL<<20};
    try {
        ure::Value source;
        for(unsigned lun=0;lun<6;++lun)for(unsigned scale:{1U,2U}) {
            const auto capacity=base[lun]*scale; const auto ranges=ure::gpt_stock_regions(inputs,capacity,lun,ure::Value(),"global-os3.0.303.0",source);
            check(source["template_preview_only"]==true && source["desired_table"]["healthy"]==true,"Stock preview is not valid and explicitly unbound");
            const auto patched=oracle(inputs,lun,capacity); const auto& main=patched.at("gpt_main"+std::to_string(lun)+".bin");
            const auto& backup=patched.at("gpt_backup"+std::to_string(lun)+".bin");
            check(ranges[0].bytes==main.substr(8192) && ranges[1].bytes==main.substr(4096,4096) &&
                ranges[2].bytes==backup.substr(0,16384) && ranges[3].bytes==backup.substr(16384) && ranges[4].bytes==main.substr(0,4096),
                "Native stock metadata differs from independent OEM XML patch oracle");
            const auto image=work/("lun"+std::to_string(lun)+"-"+std::to_string(scale)+".img"); save(image,capacity,ranges);
            auto target=ure::storage_image(image,4096,true); auto primary=ure::storage_read(target.descriptor.get(),4096,4096);
            auto secondary=ure::storage_read(target.descriptor.get(),capacity-4096,4096);
            // Distinguish the original unit's disk GUID from the OEM template.
            primary[56]^=0x51; secondary[56]^=0x51;
            for(auto* header:{&primary,&secondary}) { put(*header,16,0,4); put(*header,16,crc(std::string_view(*header).substr(0,92)),4); }
            write(target.descriptor.get(),4096,primary); write(target.descriptor.get(),capacity-4096,secondary); target=ure::storage_image(image,4096,true);
            auto observed=ure::gpt_inspect(target.descriptor.get(),4096); auto entries=ure::storage_read(target.descriptor.get(),8192,16384);
            for(const auto& part:observed["partitions"])entries[(part["index"].asUInt()-1)*128+16]^=0x29;
            for(const auto& part:observed["reserved_records"])entries[(part["index"].asUInt()-1)*128+16]^=0x29;
            update_tables(target.descriptor.get(),capacity,entries,observed["primary"]["entry_count"].asUInt()); target=ure::storage_image(image,4096,true);
            const auto original=ure::gpt_inspect(target.descriptor.get(),4096);
            const auto terminal=original["partitions"].size();
            const auto terminal_index=original["reserved_records"].empty() ? original["partitions"][terminal-1]["index"].asUInt() : original["reserved_records"][0]["index"].asUInt();
            // A vendor reservation remains a reservation through inspection.
            // Perturb a normal partition when that reservation cannot shrink.
            const auto edited_index=terminal_index==terminal ? terminal_index : original["partitions"][0]["index"].asUInt();
            put(entries,(edited_index-1)*128+40,le(entries,(edited_index-1)*128+40,8)-1,8);
            update_tables(target.descriptor.get(),capacity,entries,original["primary"]["entry_count"].asUInt()); target=ure::storage_image(image,4096,true);
            const auto before=ure::gpt_regions(target.descriptor.get(),4096);
            const auto plan=ure::gpt_stock_plan(target,inputs,lun,"global-os3.0.303.0");
            check(plan["desired_table"]["disk_guid"]==original["disk_guid"] && plan["layout_changes"].size()==1 && plan["restores_partition_contents"]==false,"Stock plan lost original disk identity or understated effects");
            for(Json::ArrayIndex i=0;i<terminal;++i)check(plan["desired_table"]["partitions"][i]["partuuid"]==original["partitions"][i]["partuuid"],"Stock reconstruction changed a partition identity");
            reject([&]{ure::gpt_execute(target,plan,work/"wrong", "bad");},"confirmation-required");
            const auto journal=work/("journal"+std::to_string(lun)+"-"+std::to_string(scale));
            check(ure::gpt_execute(target,plan,journal,plan["plan_sha256"].asString())["state"]=="COMMITTED","Stock execute did not commit");
            target=ure::storage_image(image,4096,true);
            check(ure::gpt_journal_inspect(target,journal)["classification"]=="TARGET_CONTENT_VERIFIED","Stock readback was not verified");
            ure::gpt_rollback(target,journal,plan["plan_sha256"].asString());
            for(const auto& range:before)check(ure::storage_read(target.descriptor.get(),range.offset,range.bytes.size())==range.bytes,"Stock rollback changed original metadata");
        }
        reject([&]{ure::gpt_stock_regions(inputs,base[0],0,ure::Value(),"cn-os3.0.302.0",source);},"wrong-profile");
        reject([&]{ure::gpt_stock_regions(inputs,base[0],6,ure::Value(),"global-os3.0.303.0",source);},"invalid-lun");
        reject([&]{ure::gpt_stock_regions(inputs,base[0]+1,0,ure::Value(),"global-os3.0.303.0",source);},"invalid-size");
        reject([&]{ure::gpt_stock_regions(inputs,1ULL<<20,0,ure::Value(),"global-os3.0.303.0",source);},"invalid-stock-layout");
        const auto large=ure::gpt_stock_regions(inputs,1ULL<<45,0,ure::Value(),"global-os3.0.303.0",source);
        check(le(large[4].bytes,458,4)==UINT32_MAX && source["desired_table"]["backup"]["current_lba"].asUInt64()==(1ULL<<33)-1,
            "Large capacity truncated a GPT LBA or protective MBR count");
        const auto image=work/"lun0-1.img"; auto target=ure::storage_image(image,4096,true);
        const auto original_backup=work/"original"; ure::gpt_backup(target,original_backup,"global-os3.0.303.0");
        auto entries=ure::storage_read(target.descriptor.get(),8192,16384); const auto saved_entries=entries;
        entries.replace(0,128,128,'\0'); update_tables(target.descriptor.get(),base[0],entries,32); target=ure::storage_image(image,4096,true);
        reject([&]{ure::gpt_stock_plan(target,inputs,0,"global-os3.0.303.0");},"identity-unavailable");
        const auto plan=ure::gpt_stock_plan(target,inputs,0,"global-os3.0.303.0",original_backup);
        check(plan["layout_changes"].size()==2,"Missing partition was not disclosed");
        ure::gpt_execute(target,plan,work/"original-restore",plan["plan_sha256"].asString());
        target=ure::storage_image(image,4096,true); ure::gpt_rollback(target,work/"original-restore",plan["plan_sha256"].asString());
        update_tables(target.descriptor.get(),base[0],saved_entries,32); target=ure::storage_image(image,4096,true);
        reject([&]{ure::gpt_stock_plan(target,inputs,1,"global-os3.0.303.0");},"identity-unavailable");
        auto unclassified=saved_entries; unclassified.replace(32*128,128,saved_entries.substr(0,128));
        put(unclassified,31*128+40,base[0]/4096-22,8);
        unclassified[32*128+16]^=0x22; put(unclassified,32*128+32,base[0]/4096-21,8); put(unclassified,32*128+40,base[0]/4096-6,8);
        unclassified.replace(32*128+56,72,72,'\0'); const std::string alien="unclassified";
        for(std::size_t i=0;i<alien.size();++i)unclassified[32*128+56+i*2]=alien[i];
        update_tables(target.descriptor.get(),base[0],unclassified,64); target=ure::storage_image(image,4096,true);
        check(ure::gpt_inspect(target.descriptor.get(),4096)["healthy"]==true,"Unknown-partition fixture is invalid");
        reject([&]{ure::gpt_stock_plan(target,inputs,0,"global-os3.0.303.0",original_backup);},"protected-partition");
        update_tables(target.descriptor.get(),base[0],saved_entries,32); target=ure::storage_image(image,4096,true);
        const auto failing_plan=ure::gpt_stock_plan(target,inputs,0,"global-os3.0.303.0");
        ure::gpt_execute(target,failing_plan,work/"partial",failing_plan["plan_sha256"].asString());
        const auto before_header=ure::bounded_read(work/"partial/before-primary_header.bin",4096);
        write(target.descriptor.get(),4096,std::string_view(before_header).substr(0,18));
        ure::Root partial(work/"partial"); auto state=ure::json_file(work/"partial/journal.json"); state["state"]="FAILED_UNCERTAIN"; state["verified"]=false;
        partial.save_record("journal.json",state,true);
        target=ure::storage_image(image,4096,true);
        check(ure::gpt_journal_inspect(target,work/"partial")["classification"]=="PARTIAL_EXPECTED_WRITE","Simulated partial stock write was not classified from its bytes");
        ure::gpt_rollback(target,work/"partial",failing_plan["plan_sha256"].asString()); target=ure::storage_image(image,4096,true);
        const auto copy=work/"inputs"; ure::fs::create_directory(copy);
        for(const auto& entry:ure::fs::directory_iterator(inputs))if(entry.is_regular_file() && (entry.path().filename().string().starts_with("gpt_") || entry.path().filename().string().starts_with("patch") || entry.path().filename().string().starts_with("rawprogram")))ure::fs::copy_file(entry.path(),copy/entry.path().filename());
        const auto changed_plan=ure::gpt_stock_plan(target,copy,0,"global-os3.0.303.0");
        ure::Fd changed(::open((copy/"patch0.xml").c_str(),O_RDWR|O_CLOEXEC)); write(changed.get(),0,"X");
        reject([&]{ure::gpt_execute(target,changed_plan,work/"changed-source",changed_plan["plan_sha256"].asString());},"stock-source-mismatch");
        check(!ure::fs::exists(work/"changed-source"),"Changed source created a journal");
        ure::fs::remove_all(work); std::cout<<"Six-LUN stock GPT: independent OEM XML oracle, two capacities, original GUIDs, reviewed execute/readback/rollback, original-backup recovery and refusal fixtures passed; sparse images only.\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; ure::fs::remove_all(work); return 1; }
}
