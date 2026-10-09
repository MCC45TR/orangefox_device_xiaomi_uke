// SPDX-License-Identifier: Apache-2.0
// Disposable regular images only. Host process interruption is not tablet HIL.
#include "uke.h"
#include <algorithm>
#include <array>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

namespace {
#ifndef URE_PARTITION_FIXTURE_CAPACITY_MIB
#define URE_PARTITION_FIXTURE_CAPACITY_MIB 512
#endif
constexpr std::uint64_t mib=1048576,capacity=URE_PARTITION_FIXTURE_CAPACITY_MIB*mib;
void check(bool value,const std::string& message) { if(!value)throw std::runtime_error(message); }
template<class F> void reject(F action,const std::string& code) {
    try { action(); } catch(const ure::Error& e) { check(e.code==code,"Unexpected refusal "+e.code+", wanted "+code); return; } throw std::runtime_error("Expected refusal "+code);
}
std::string digest(const ure::fs::path& path) { ure::Fd fd(::open(path.c_str(),O_RDONLY|O_NOFOLLOW)); return ure::sha256(fd.get()); }
void put(std::string& bytes,std::size_t offset,std::uint64_t value,unsigned count) { for(unsigned i=0;i<count;++i)bytes.at(offset+i)=static_cast<char>((value>>(8*i))&255); }
std::uint32_t crc(std::string_view data) { std::uint32_t out=UINT32_MAX;
    for(char byte:data) { out^=static_cast<unsigned char>(byte); for(unsigned i=0;i<8;++i)out=(out>>1)^((out&1U) ? 0xedb88320U : 0U); } return out^UINT32_MAX; }
void write(int fd,const std::string& data,std::uint64_t offset) { check(::pwrite(fd,data.data(),data.size(),static_cast<off_t>(offset))==static_cast<ssize_t>(data.size()),"Fixture write failed"); }
void extract(int source,const ure::fs::path& path,std::uint64_t offset,std::uint64_t bytes) {
    ure::Fd out(::open(path.c_str(),O_RDWR|O_CREAT|O_EXCL,0600)); check(out.get()>=0 && ::ftruncate(out.get(),static_cast<off_t>(bytes))==0,"Cannot extract fixture range");
    for(std::uint64_t at=0;at<bytes;) { const auto data=ure::storage_read(source,offset+at,static_cast<std::size_t>(std::min<std::uint64_t>(65536,bytes-at)));
        if(std::any_of(data.begin(),data.end(),[](char byte) { return byte!=0; }))write(out.get(),data,at);
        at+=data.size(); }
    check(::fsync(out.get())==0,"Cannot sync fixture range");
}
void copy_into(int destination,const ure::fs::path& path,std::uint64_t offset) {
    ure::Fd source(::open(path.c_str(),O_RDONLY|O_NOFOLLOW)); const auto bytes=ure::storage_bytes(source.get());
    for(std::uint64_t at=0;at<bytes;) { const auto data=ure::storage_read(source.get(),at,static_cast<std::size_t>(std::min<std::uint64_t>(65536,bytes-at))); write(destination,data,offset+at); at+=data.size(); }
    check(::fsync(destination)==0,"Cannot sync fixture payload");
}
void debugfs(const ure::fs::path& file,const std::string& command) {
    const auto child=::fork(); check(child>=0,"Cannot fork host-only debugfs");
    if(child==0) { ure::Fd null(::open("/dev/null",O_WRONLY)); ::dup2(null.get(),1); ::dup2(null.get(),2);
        ::execlp("debugfs","debugfs","-w","-R",command.c_str(),file.c_str(),static_cast<char*>(nullptr)); ::_exit(127); }
    int status=0; check(::waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0,"Host-only debugfs fixture failed");
}
ure::Value request() {
    ure::Value value; value["schema"]=1; value["format"]="ure-layout-request"; value["rows"]=ure::Value(Json::arrayValue);
    const std::array<std::string,4> names{"esp","linux","windows","userdata"},sizes{"64","64","64",""},types{"fat32","ext4","ntfs","ext4"};
    for(unsigned i=0;i<4;++i) { ure::Value row; row["role"]=names[i]; row["size"]=sizes[i]; row["unit"]=i==3 ? "remaining" : "MiB"; row["filesystem"]=types[i]; value["rows"].append(row); } return value;
}
void fixture(const ure::fs::path& path,unsigned sector,bool shared=false,bool relocate=false,bool compatible=false) {
    ure::Fd fd(::open(path.c_str(),O_RDWR|O_CREAT|O_EXCL,0600)); check(fd.get()>=0 && ::ftruncate(fd.get(),static_cast<off_t>(capacity))==0,"Cannot create GPT fixture");
    const auto sectors=capacity/sector; constexpr unsigned count=128,table_bytes=count*128; const auto last=sectors-2-table_bytes/sector,backup=last+1;
    std::string entries(table_bytes,'\0');
    auto entry=[&](unsigned slot,std::uint64_t begin,std::uint64_t end,const std::string& name,bool esp=false) {
        const auto offset=slot*128; for(unsigned i=0;i<16;++i)entries[offset+i]=static_cast<char>(i+11);
        if(esp) { constexpr std::array<unsigned char,16> type{0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b};
            for(unsigned i=0;i<16;++i)entries[offset+i]=static_cast<char>(type[i]); }
        for(unsigned i=0;i<16;++i)entries[offset+16+i]=static_cast<char>(i+51);
        put(entries,offset+16,slot+1,4); put(entries,offset+32,begin,8); put(entries,offset+40,end,8);
        for(std::size_t i=0;i<name.size();++i)entries[offset+56+i*2]=name[i];
    };
    const std::uint64_t start=(shared ? 80 : 16)*mib;
    entry(0,(shared ? 65 : 1)*mib/sector,start/sector-1,"super"); entry(1,start/sector,(capacity-mib)/sector-1,"userdata");
    entry(2,(capacity-mib)/sector,last,"oem_reserved"); if(shared)entry(3,mib/sector,65*mib/sector-1,"uke_esp",true);
    write(fd.get(),entries,2*sector); write(fd.get(),entries,backup*sector);
    for(const auto lba:std::array<std::uint64_t,2>{1,sectors-1}) {
        std::string header(sector,'\0'); header.replace(0,8,"EFI PART"); put(header,8,0x10000,4); put(header,12,92,4);
        put(header,24,lba,8); put(header,32,sectors-lba,8); put(header,40,2+table_bytes/sector,8); put(header,48,last,8);
        for(unsigned i=0;i<16;++i)header[56+i]=static_cast<char>(i+81);
        put(header,72,lba==1 ? 2 : backup,8); put(header,80,count,4); put(header,84,128,4); put(header,88,crc(entries),4); put(header,16,crc(std::string_view(header).substr(0,92)),4); write(fd.get(),header,lba*sector);
    }
    std::string mbr(sector,'\0'); mbr[450]=static_cast<char>(0xee); put(mbr,454,1,4); put(mbr,458,sectors-1,4); mbr[510]=0x55; mbr[511]=static_cast<char>(0xaa); write(fd.get(),mbr,0);
    write(fd.get(),"protected-super-data",(shared ? 66 : 2)*mib); write(fd.get(),"protected-reservation",capacity-mib);
    const auto userdata=path.parent_path()/"fixture-userdata.img",payload=path.parent_path()/"payload.bin";
    ure::Fd data(::open(userdata.c_str(),O_RDWR|O_CREAT|O_TRUNC,0600)); check(data.get()>=0 && ::ftruncate(data.get(),static_cast<off_t>(capacity-mib-start))==0,"Cannot size ext4 fixture");
    const auto formatted=ure::run_tool("mke2fs",{"-q","-F","-t","ext4","-O",compatible ? "^encrypt,^orphan_file" : "^encrypt",userdata.string()},60); check(formatted.status==0,"Cannot format ext4 fixture");
    { std::ofstream source(payload,std::ios::binary); source<<std::string(256*1024,'K'); check(source.good(),"Cannot write retained file"); }
    if(relocate) {
        const auto filler=path.parent_path()/"filler.bin"; { std::ofstream file(filler,std::ios::binary); const std::string block(mib,'B'); for(unsigned i=0;i<350;++i)file<<block; check(file.good(),"Cannot create relocation fixture"); }
        debugfs(userdata,"write "+filler.string()+" /filler"); debugfs(userdata,"write "+payload.string()+" /kept"); debugfs(userdata,"rm /filler"); ure::fs::remove(filler);
    } else debugfs(userdata,"write "+payload.string()+" /kept");
    copy_into(fd.get(),userdata,start); ure::fs::remove(userdata);
    if(shared) { const auto esp=path.parent_path()/"fixture-esp.img"; ure::Fd image(::open(esp.c_str(),O_RDWR|O_CREAT|O_EXCL,0600)); check(image.get()>=0 && ::ftruncate(image.get(),64*mib)==0,"Cannot size shared ESP");
        check(ure::run_tool("mkfs.fat",{"-F","32",esp.string()},60).status==0,"Cannot format shared ESP"); copy_into(fd.get(),esp,mib); ure::fs::remove(esp); }
    check(::fsync(fd.get())==0,"Cannot sync disk fixture");
}
void retained(const ure::fs::path& image,const ure::Value& plan) {
    auto target=ure::storage_image(image,plan["target_identity"]["logical_sector_bytes"].asUInt());
    for(const auto& row:plan["gpt"]["layout"]["rows"])if(row["enabled"]==true) {
        check(ure::filesystem_probe_range(target.descriptor.get(),row["offset"].asUInt64(),row["bytes"].asUInt64())["type"]==(row["filesystem"]=="fat32" ? "vfat" : row["filesystem"].asString()),"New role contains the wrong filesystem");
        if(row["role"]=="userdata" && plan["gpt"]["layout"]["userdata_policy"]=="preserve") {
            const auto extract_path=image.parent_path()/"checked-userdata.img",dump=image.parent_path()/"kept.bin";
            extract(target.descriptor.get(),extract_path,row["offset"].asUInt64(),row["bytes"].asUInt64()); debugfs(extract_path,"dump /kept "+dump.string());
            check(ure::bounded_read(dump,1024*1024)==std::string(256*1024,'K'),"Shrunk userdata lost the actual retained file"); ure::fs::remove(dump); ure::fs::remove(extract_path);
        }
    }
}
void killed(const ure::fs::path& image,const ure::fs::path& journal,const ure::Value& plan) {
    const auto child=::fork(); check(child>=0,"Cannot fork job interruption");
    if(child==0) { try { ure::Root system("/"); auto target=ure::storage_image(image,4096,true); ure::partition_job_execute(system,target,plan,journal,plan["plan_sha256"].asString()); ::_exit(0); } catch(...) { ::_exit(2); } }
    bool observed=false;
    for(unsigned tries=0;tries<120000;++tries) {
        try { if(ure::fs::exists(journal/"state.json")) { const auto state=ure::json_file(journal/"state.json"); observed=state["state"]=="APPLYING" && state["last_verified_chunk"].isUInt() && state["last_verified_chunk"].asUInt()>2; } } catch(const ure::Error&) {}
        int status=0; if(::waitpid(child,&status,WNOHANG)==child)throw std::runtime_error("Writer exited before interruption boundary");
        if(observed)break;
        ::usleep(1000);
    }
    ::kill(child,SIGSTOP); int status=0; check(::waitpid(child,&status,WUNTRACED)==child,"Cannot stop writer");
    if(WIFSTOPPED(status)) { ::kill(child,SIGKILL); check(::waitpid(child,&status,0)==child,"Cannot reap writer"); }
    check(observed && WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL,"Did not interrupt an original-target write");
}
struct Workspace { ure::fs::path path;
    Workspace() { std::array<char,64> buffer{}; const std::string pattern="/tmp/ure-partition-job-XXXXXX"; std::copy(pattern.begin(),pattern.end(),buffer.begin()); const auto* value=::mkdtemp(buffer.data()); check(value,"mkdtemp failed"); path=value; }
    ~Workspace() { std::error_code error; ure::fs::remove_all(path,error); }
};
}
int main(int argc,char* argv[]) {
    try {
        if(argc==3 && std::string_view(argv[1])=="--fixture") { fixture(argv[2],4096); return 0; }
        if(argc==3 && std::string_view(argv[1])=="--fixture-compatible") { fixture(argv[2],4096,false,false,true); return 0; }
        if(argc==2 && std::string_view(argv[1])=="--request") { std::cout<<ure::json(request()); return 0; }
        Workspace work; ure::Root system("/");
        for(unsigned sector:{512U,4096U}) {
            const auto image=work.path/(std::to_string(sector)+".img"),journal=work.path/("job-"+std::to_string(sector)); fixture(image,sector,false,sector==512);
            const auto original=digest(image); auto target=ure::storage_image(image,sector); auto choices=request();
            auto mismatch=choices; mismatch["rows"][3]["filesystem"]="f2fs"; reject([&] { ure::partition_job_plan(system,target,mismatch,"fixture"); },"userdata-filesystem-mismatch");
            auto plan=ure::partition_job_plan(system,target,choices,"fixture"); check(plan["execution_scope"]=="FILESYSTEMS_AND_GPT_WITHIN_ORIGINAL_USERDATA" && plan["live_write_backend_ready"]==false &&
                plan["image_job_only"]==true && plan["live_repartition_blockers"].size()==9,"False job scope, missing live blockers or hardware claim");
            const auto confirmation=plan["plan_sha256"].asString(); auto writer=ure::storage_image(image,sector,true);
            reject([&] { ure::partition_job_execute(system,writer,plan,journal,"bad"); },"confirmation-required"); check(!ure::fs::exists(journal),"Bad confirmation created a journal");
            const auto done=ure::partition_job_execute(system,writer,plan,journal,confirmation); check(done["state"]=="COMMITTED" && done["verified"]==true && done["complete_partition_job"]==true,"Combined filesystem/GPT job did not commit"); retained(image,plan);
            auto reviewed=ure::partition_job_recover(system,writer,journal,"inspect"); check(reviewed["classification"]=="TARGET" && reviewed["protected_ranges_verified"]==true,"Committed bytes do not verify");
            const auto copy_path=work.path/"wrong.img"; ure::fs::copy_file(image,copy_path); auto wrong=ure::storage_image(copy_path,sector,true);
            reject([&] { ure::partition_job_recover(system,wrong,journal,"rollback",confirmation); },"wrong-target"); ure::fs::remove(copy_path);
            // A forged counter cannot hide a partial old/new chunk. Recovery
            // compares bytes and completes the entire reviewed destination.
            auto state=ure::json_file(journal/"state.json"); state["state"]="APPLYING"; state["written_bytes"]=Json::UInt64(UINT64_MAX); ure::save_json(journal/"state.json",state,true);
            ure::Root store(journal); auto before=store.open("userdata-before.img",O_RDONLY); write(writer.descriptor.get(),ure::storage_read(before.get(),0,65536),16*mib); check(::fsync(writer.descriptor.get())==0,"Cannot simulate partial expected write");
            reviewed=ure::partition_job_recover(system,writer,journal,"inspect"); check(reviewed["classification"]=="PARTIAL_EXPECTED_WRITE","Partial expected bytes were not recognized");
            check(ure::partition_job_recover(system,writer,journal,"resume",confirmation)["state"]=="COMMITTED","Partial write did not resume"); retained(image,plan);
            check(ure::partition_job_recover(system,writer,journal,"rollback",confirmation)["state"]=="ROLLED_BACK" && digest(image)==original,"Rollback did not restore the entire original image");
            // Detect filesystem encryption independently of container signatures.
            auto writable=ure::storage_image(image,sector,true); const auto feature=ure::storage_read(writable.descriptor.get(),16*mib+1120,4); auto encrypted=feature; encrypted[2]=static_cast<char>(static_cast<unsigned char>(encrypted[2])|1U); write(writable.descriptor.get(),encrypted,16*mib+1120); check(::fsync(writable.descriptor.get())==0,"Cannot set encryption fixture");
            auto crypt=ure::storage_image(image,sector); reject([&] { ure::partition_job_plan(system,crypt,choices,"fixture"); },"userdata-encryption-unverified"); write(writable.descriptor.get(),feature,16*mib+1120); check(::fsync(writable.descriptor.get())==0,"Cannot restore encryption fixture");
            ure::fs::remove(image); ure::fs::remove_all(journal);
        }
        // Front placement explicitly recreates userdata; it never migrates an
        // encrypted filesystem or borrows a preservation claim from an image.
        const auto front_image=work.path/"front.img"; fixture(front_image,4096); const auto front_before=digest(front_image);
        auto front_choices=request(); front_choices["placement"]="before_userdata"; auto front_target=ure::storage_image(front_image,4096);
        reject([&]{ure::partition_job_plan(system,front_target,front_choices,"fixture");},"userdata-migration-required");
        front_choices["mode"]="advanced"; front_choices["userdata_policy"]="recreate";
        const auto front_plan=ure::partition_job_plan(system,front_target,front_choices,"fixture"); const auto front_journal=work.path/"front-job";
        check(front_plan["preserves_userdata_files"]==false && front_plan["before_userdata_data_migration"]==false &&
            front_plan["gpt"]["layout"]["rows"][3]["destroys_existing_data"]==true && front_plan["android_userdata_boot_compatibility_verified"]==false,
            "Front recreation claimed preserved data, encryption migration or Android boot acceptance");
        auto front_writer=ure::storage_image(front_image,4096,true);
        check(ure::partition_job_execute(system,front_writer,front_plan,front_journal,front_plan["plan_sha256"].asString())["state"]=="COMMITTED","Explicit front recreation did not commit");
        retained(front_image,front_plan);
        const auto& front_userdata=front_plan["gpt"]["layout"]["rows"][3]; const auto recreated=work.path/"front-recreated.img",missing=work.path/"should-not-exist.bin";
        extract(front_writer.descriptor.get(),recreated,front_userdata["offset"].asUInt64(),front_userdata["bytes"].asUInt64());
        debugfs(recreated,"dump /kept "+missing.string()); check(!ure::fs::exists(missing),"Recreated userdata retained the original file unexpectedly");
        check(ure::partition_job_recover(system,front_writer,front_journal,"rollback",front_plan["plan_sha256"].asString())["state"]=="ROLLED_BACK" &&
            digest(front_image)==front_before,"Front recreation rollback did not restore every original byte");
        const auto image=work.path/"shared.img",journal=work.path/"shared-job"; fixture(image,4096,true); const auto original=digest(image); auto choices=request(); choices["rows"][0]["size"]="0";
        auto selected=ure::storage_image(image,4096); const auto shared_plan=ure::partition_job_plan(system,selected,choices,"fixture"); auto writer=ure::storage_image(image,4096,true);
        const auto done=ure::partition_job_execute(system,writer,shared_plan,journal,shared_plan["plan_sha256"].asString()); check(done["protected_ranges_verified"]==true,"Existing shared ESP was not protected");
        check(ure::partition_job_recover(system,writer,journal,"rollback",shared_plan["plan_sha256"].asString())["state"]=="ROLLED_BACK" && digest(image)==original,"Shared ESP rollback changed protected bytes"); ure::fs::remove_all(journal);
        // Host SIGKILL provides source-level interruption evidence only.
        auto clean=ure::storage_image(image,4096); const auto plan=ure::partition_job_plan(system,clean,choices,"fixture"); killed(image,work.path/"killed",plan);
        auto interrupted=ure::storage_image(image,4096,true); const auto status=ure::partition_job_recover(system,interrupted,work.path/"killed","inspect"); check(status["classification"]=="PARTIAL_EXPECTED_WRITE","Interrupted job did not classify readback");
        check(ure::partition_job_recover(system,interrupted,work.path/"killed","resume",plan["plan_sha256"].asString())["state"]=="COMMITTED","Killed writer did not resume"); retained(image,plan);
        const auto manifest=ure::json_file(work.path/"killed/application.json"); const auto& chunk=manifest["chunks"][0]; const auto offset=chunk["offset"].asUInt64();
        ure::Root store(work.path/"killed"); auto old=store.open(chunk["before_file"].asString(),O_RDONLY),next=store.open(chunk["after_file"].asString(),O_RDONLY);
        const auto was=ure::storage_read(old.get(),chunk["before_offset"].asUInt64(),1),will=ure::storage_read(next.get(),chunk["after_offset"].asUInt64(),1),current=ure::storage_read(interrupted.descriptor.get(),offset,1);
        unsigned value=0; while(value==static_cast<unsigned char>(was[0]) || value==static_cast<unsigned char>(will[0]))++value;
        write(interrupted.descriptor.get(),std::string(1,static_cast<char>(value)),offset); check(::fsync(interrupted.descriptor.get())==0,"Cannot create divergence");
        const auto diverged=ure::partition_job_recover(system,interrupted,work.path/"killed","inspect"); check(diverged["classification"]=="DIVERGED" && diverged["recovery_actions"].empty(),"Unrelated data was accepted as a torn write");
        reject([&] { ure::partition_job_recover(system,interrupted,work.path/"killed","rollback",plan["plan_sha256"].asString()); },"unsafe-recovery"); write(interrupted.descriptor.get(),current,offset); check(::fsync(interrupted.descriptor.get())==0,"Cannot restore divergence fixture");
        check(ure::partition_job_recover(system,interrupted,work.path/"killed","rollback",plan["plan_sha256"].asString())["state"]=="ROLLED_BACK" && digest(image)==original,"Interrupted job rollback changed the original disk");
        std::cout<<"Combined partition jobs: ext4 file relocation/shrink, FAT32/ext4/NTFS creation, 512/4096 GPT, shared ESP protection, complete byte rollback, exact confirmation/inode binding, fscrypt refusal, partial readback/counter distrust, SIGKILL resume and unrelated-divergence refusal passed; synthetic images and host tools only.\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
