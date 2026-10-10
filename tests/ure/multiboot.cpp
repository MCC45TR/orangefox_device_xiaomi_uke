// SPDX-License-Identifier: Apache-2.0
// Sparse metadata fixtures never mount or select host/tablet block devices.
#define URE_PARTITION_FIXTURE_CAPACITY_MIB 1024
#define main partition_job_fixture_main
#include "partition_job.cpp"
#undef main

namespace {
void metadata_fixture(const ure::fs::path& path,unsigned sector) {
    constexpr std::uint64_t bytes=256ULL*1073741824;
    ure::Fd fd(::open(path.c_str(),O_RDWR|O_CREAT|O_EXCL,0600));
    check(fd.get()>=0 && ::ftruncate(fd.get(),static_cast<off_t>(bytes))==0,"Cannot size sparse metadata fixture");
    const auto sectors=bytes/sector; constexpr unsigned table_size=128*128;
    const auto last=sectors-2-table_size/sector,backup=last+1;
    std::string entries(table_size,'\0');
    auto entry=[&](unsigned index,std::uint64_t first,std::uint64_t end,const std::string& label) {
        const auto offset=index*128;
        for(unsigned i=0;i<16;++i) { entries[offset+i]=static_cast<char>(i+11); entries[offset+16+i]=static_cast<char>(i+31+index); }
        put(entries,offset+32,first,8); put(entries,offset+40,end,8);
        for(std::size_t i=0;i<label.size();++i)entries[offset+56+2*i]=label[i];
    };
    entry(0,mib/sector,2*mib/sector-1,"super");
    entry(1,4*mib/sector,last-256,"userdata");
    write(fd.get(),entries,2*sector); write(fd.get(),entries,backup*sector);
    for(const auto lba:std::array<std::uint64_t,2>{1,sectors-1}) {
        std::string header(sector,'\0'); header.replace(0,8,"EFI PART"); put(header,8,0x10000,4); put(header,12,92,4);
        put(header,24,lba,8); put(header,32,sectors-lba,8); put(header,40,2+table_size/sector,8); put(header,48,last,8);
        for(unsigned i=0;i<16;++i)header[56+i]=static_cast<char>(i+81);
        put(header,72,lba==1 ? 2 : backup,8); put(header,80,128,4); put(header,84,128,4); put(header,88,crc(entries),4);
        put(header,16,crc(std::string_view(header).substr(0,92)),4); write(fd.get(),header,lba*sector);
    }
    std::string mbr(sector,'\0'); mbr[450]=static_cast<char>(0xee); put(mbr,454,1,4); put(mbr,458,std::min<std::uint64_t>(sectors-1,UINT32_MAX),4);
    mbr[510]=0x55; mbr[511]=static_cast<char>(0xaa); write(fd.get(),mbr,0); check(::fsync(fd.get())==0,"Cannot sync sparse GPT");
}
ure::Value multiboot_request() {
    ure::Value out; out["schema"]=1; out["format"]="uke-multiboot-request"; out["action"]="setup";
    out["advanced"]=false; out["keep_userdata"]=true; out["userdata_filesystem"]="ext4"; out["partitions"]=ure::Value(Json::arrayValue); return out;
}
void select(ure::Value& request,const char* role,const char* size,const char* unit,const char* filesystem) {
    ure::Value row; row["role"]=role; row["size"]=size; row["unit"]=unit; row["filesystem"]=filesystem; request["partitions"].append(row);
}
}
int main() {
    try {
        Workspace work; ure::Root system("/");
        for(unsigned sector:{512U,4096U}) {
            const auto path=work.path/("metadata-"+std::to_string(sector)); metadata_fixture(path,sector);
            auto target=ure::storage_image(path,sector); const auto before=ure::gpt_regions(target.descriptor.get(),sector);
            auto request=multiboot_request(); select(request,"esp","512","MiB","fat32"); select(request,"linux_boot","512","MiB","ext4");
            select(request,"linux","40","GiB","ext4"); select(request,"windows","50","GiB","ntfs");
            select(request,"shared","1","GiB","exfat"); select(request,"linux_swap","1","GiB","linux-swap"); select(request,"linux2","40","GiB","btrfs");
            const auto plan=ure::gpt_multiboot_plan(target,request,{},"fixture",&system);
            const auto& rows=plan["layout"]["rows"]; check(rows.size()==8,"Missing multiboot roles");
            check(rows[0]["role"]=="userdata" && rows[1]["role"]=="esp" && rows[2]["role"]=="linux_boot" && rows[3]["role"]=="linux","Unexpected standard order");
            check(rows[0]["bytes"].asUInt64()>=64ULL*1073741824,"Userdata minimum was lost");
            for(const auto* side:{"old_rows","rows"}) {
                ure::Value graph; graph["format"]="ure-multiboot-bar"; graph["pool"]=plan["layout"]["pool"]; graph["rows"]=plan["layout"][side];
                for(unsigned width:{1U,540U,918U,2136U,3200U,32768U}) {
                    unsigned cursor=0;
                    for(const auto& segment:ure::multiboot_layout_bar(graph,width)) {
                        check(segment["x"].asUInt()==cursor,"Comparison graph contains a pixel gap"); cursor+=segment["width"].asUInt();
                    }
                    check(cursor==width,"Comparison graph does not cover the display width");
                }
                auto invalid=graph; invalid["rows"][0]["bytes"]=Json::UInt64(UINT64_MAX);
                reject([&]{ure::multiboot_layout_bar(invalid,2136);},"invalid-layout-bar");
            }
            auto bad=request; bad["partitions"][0]["size"]="5"; bad["partitions"][0]["unit"]="GiB";
            reject([&]{ure::gpt_multiboot_plan(target,bad,{},"fixture",&system);},"esp-too-large");
            bad=request; bad["partitions"][2]["size"]="39";
            reject([&]{ure::gpt_multiboot_plan(target,bad,{},"fixture",&system);},"multiboot-partition-too-small");
            bad=request; bad["partitions"][0]["filesystem"]="ext4";
            reject([&]{ure::gpt_multiboot_plan(target,bad,{},"fixture",&system);},"fixed-multiboot-filesystem");
            bad=request; bad["partitions"][2]["label"]="custom";
            reject([&]{ure::gpt_multiboot_plan(target,bad,{},"fixture",&system);},"advanced-mode-required");
            bad=request; bad["partitions"][2]["size"]="70"; bad["partitions"][2]["unit"]="%";
            reject([&]{ure::gpt_multiboot_plan(target,bad,{},"fixture",&system);},"insufficient-layout-space");
            bad=multiboot_request(); select(bad,"windows","50","GiB","ntfs");
            reject([&]{ure::gpt_multiboot_plan(target,bad,{},"fixture",&system);},"windows-requires-esp");
            bad=request; bad["advanced"]=true; bad["order"]=ure::Value(Json::arrayValue);
            for(const auto* name:{"esp","linux_boot","linux","linux_swap","windows","shared","linux2","userdata"})bad["order"].append(name);
            check(ure::gpt_multiboot_plan(target,bad,{},"fixture",&system)["layout"]["rows"][0]["role"]=="esp","Advanced order was ignored");
            const auto after=ure::gpt_regions(target.descriptor.get(),sector);
            for(std::size_t i=0;i<before.size();++i)check(before[i].bytes==after[i].bytes,"Planning wrote GPT");
            const auto original_backup=work.path/("metadata-backup-"+std::to_string(sector));
            ure::gpt_backup(target,original_backup,"fixture",&system);
            auto restore=multiboot_request(); restore["action"]="restore-default";
            const auto restored=ure::gpt_multiboot_plan(target,restore,original_backup,"fixture",&system)["layout"];
            check(restored["rows"][0]["end_lba"].asUInt64()==restored["original_userdata"]["end_lba"].asUInt64(),"Restore lost the original sector-aligned userdata tail");
            auto decimal=multiboot_request(); select(decimal,"esp","512","MB","fat32");
            check(ure::gpt_multiboot_plan(target,decimal,{},"fixture",&system)["layout"]["rows"][1]["bytes"].asUInt64()>=512000000,
                "Exact decimal ESP minimum was rejected or rounded down");
        }
        const auto path=work.path/"execution.img"; fixture(path,4096); auto target=ure::storage_image(path,4096,true);
        const auto original=digest(path); const auto backup=work.path/"original-gpt"; ure::gpt_backup(target,backup,"fixture",&system);
        auto request=multiboot_request(); request["keep_userdata"]=false; select(request,"esp","512","MiB","fat32");
        const auto plan=ure::multiboot_plan(system,target,request,{},"fixture"); check(plan["executable"]==true,"Disposable ESP plan must be executable");
        reject([&]{ure::multiboot_image_execute(system,target,plan,work.path/"wrong",plan["plan_sha256"].asString(),"wrong");},"confirmation-required");
        const auto journal=work.path/"apply";
        ure::multiboot_image_execute(system,target,plan,journal,plan["plan_sha256"].asString(),"ERASE USERDATA");
        const auto inspection=ure::multiboot_inspect(target,backup,"fixture",&system); check(inspection["existing_multiboot"]==true,"New layout was not detected");
        reject([&]{ure::multiboot_inspect(target,{},"fixture",&system);},"original-gpt-required");
        auto restore=multiboot_request(); restore["action"]="restore-default";
        const auto restore_plan=ure::multiboot_plan(system,target,restore,backup,"fixture");
        check(restore_plan["new_rows"].size()==1 && restore_plan["new_rows"][0]["role"]=="userdata","Restore must create userdata only");
        ure::partition_job_recover(system,target,journal,"rollback",plan["image_job"]["plan_sha256"].asString());
        check(digest(path)==original,"Rollback did not restore original payload and GPT");
        std::cout<<"Multiboot boundaries, roles, minimums, consent, image execution and rollback passed\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
