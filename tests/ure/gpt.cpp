// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sys/file.h>
#include <unistd.h>

namespace {
void check(bool value,const std::string& text) { if(!value)throw std::runtime_error(text); }
template<class F> void reject(F action,const std::string& code) {
    try { action(); } catch(const ure::Error& error) { check(error.code==code,"Unexpected rejection "+error.code+", wanted "+code); return; }
    throw std::runtime_error("Expected rejection "+code);
}
void write(const ure::fs::path& path,const std::string& bytes) {
    ure::fs::create_directories(path.parent_path()); std::ofstream file(path,std::ios::binary); file.write(bytes.data(),static_cast<std::streamsize>(bytes.size())); check(file.good(),"Fixture write failed");
}
void le(std::string& bytes,std::size_t offset,std::uint64_t value,unsigned count) { for(unsigned i=0;i<count;++i)bytes[offset+i]=static_cast<char>((value>>(8*i))&255); }
std::uint32_t crc(std::string_view bytes) {
    std::uint32_t result=0xffffffff; for(const char b:bytes) { result^=static_cast<unsigned char>(b); for(unsigned i=0;i<8;++i)result=(result>>1)^((result&1)!=0 ? 0xedb88320 : 0); } return result^0xffffffff;
}
std::string image(unsigned sector) {
    const unsigned sectors=256,table_sectors=16384/sector,backup_table=sectors-1-table_sectors;
    std::string disk(sector*sectors,'\0'),entries(16384,'\0');
    disk[510]=0x55; disk[511]=static_cast<char>(0xaa); disk[450]=static_cast<char>(0xee); le(disk,454,1,4); le(disk,458,sectors-1,4);
    for(unsigned i=0;i<32;++i)entries[i]=static_cast<char>(i+1);
    le(entries,32,2+table_sectors,8); le(entries,40,backup_table-1,8);
    entries[56]='u'; entries[58]='k'; entries[60]='e';
    // A UTF-16 surrogate pair must become one valid UTF-8 scalar in inspection.
    le(entries,62,0xd83d,2); le(entries,64,0xde80,2);
    disk.replace(sector*2,entries.size(),entries); disk.replace(sector*backup_table,entries.size(),entries);
    for(unsigned lba:{1U,sectors-1}) {
        std::string header(sector,'\0'); header.replace(0,8,"EFI PART"); le(header,8,0x10000,4); le(header,12,92,4);
        le(header,24,lba,8); le(header,32,sectors-lba,8); le(header,40,2+table_sectors,8); le(header,48,backup_table-1,8);
        for(unsigned i=0;i<16;++i)header[56+i]=static_cast<char>(i+41);
        le(header,72,lba==1 ? 2 : backup_table,8); le(header,80,128,4); le(header,84,128,4); le(header,88,crc(entries),4);
        le(header,16,crc(header.substr(0,92)),4); disk.replace(lba*sector,sector,header);
    }
    return disk;
}
void change(const ure::fs::path& path,std::uint64_t offset,std::string_view bytes) {
    ure::Fd fd(::open(path.c_str(),O_RDWR|O_CLOEXEC)); check(fd.get()>=0,"Cannot open fixture");
    check(::pwrite(fd.get(),bytes.data(),bytes.size(),static_cast<off_t>(offset))==static_cast<ssize_t>(bytes.size()) && ::fsync(fd.get())==0,"Cannot change fixture");
}
}
int main(int argc,char* argv[]) {
    if(argc==3 && std::string_view(argv[1])=="--fixture") { write(argv[2],image(4096)); return 0; }
    std::array<char,32> temp{}; const std::string pattern="/tmp/ure-gpt-tests-XXXXXX"; std::copy(pattern.begin(),pattern.end(),temp.begin());
    const auto raw=::mkdtemp(temp.data()); if(!raw)return 1; const ure::fs::path work(raw);
    try {
        for(unsigned sector:{512U,4096U}) {
            const auto base=work/std::to_string(sector); ure::fs::create_directory(base); const auto file=base/"disk.img";
            const auto pristine=image(sector); write(file,pristine);
            auto target=ure::storage_image(file,sector);
            const auto inspection=ure::gpt_inspect(target.descriptor.get(),sector);
            check(inspection["healthy"]==true && inspection["partitions"][0]["label"]=="uke🚀","GPT layout or UTF-16 decoding failed");
            auto manifest=ure::gpt_backup(target,base/"backup","fixture");
            check(ure::gpt_backup_verify(base/"backup")["verified"]==true && ure::gpt_compare(target,base/"backup","fixture")["matches"]==true,"GPT backup or compare failed");
            {
                ure::Root store(base/"backup"); const auto original_table=store.read("partition-table.json"),original_sums=store.read("SHA256SUMS");
                auto legacy_table=ure::parse_json(original_table); legacy_table.removeMember("reserved_records");
                auto legacy_manifest=manifest; legacy_manifest["partition_table_sha256"]=ure::sha256(ure::json(legacy_table));
                legacy_manifest.removeMember("manifest_sha256"); legacy_manifest["manifest_sha256"]=ure::sha256(ure::json(legacy_manifest));
                store.save_record("partition-table.json",legacy_table,true); store.save_record("manifest.json",legacy_manifest,true);
                std::string sums;
                for(const auto& range:legacy_manifest["regions"])sums+=range["sha256"].asString()+"  "+range["name"].asString()+".bin\n";
                sums+=legacy_manifest["partition_table_sha256"].asString()+"  partition-table.json\n"+ure::sha256(ure::json(legacy_manifest))+"  manifest.json\n";
                store.atomic_save("SHA256SUMS",sums,ure::sha256(original_sums));
                check(ure::gpt_backup_verify(base/"backup")["verified"]==true,"Older schema-1 backup without an empty reserved-record inventory was rejected");
                store.save_record("partition-table.json",ure::parse_json(original_table),true); store.save_record("manifest.json",manifest,true);
                store.atomic_save("SHA256SUMS",original_sums,ure::sha256(sums));
            }
            reject([&]{ure::gpt_compare(target,base/"backup","other-profile");},"wrong-profile");
            write(base/"copy.img",pristine); auto other=ure::storage_image(base/"copy.img",sector);
            reject([&]{ure::gpt_compare(other,base/"backup","fixture");},"wrong-target");
            auto altered=pristine; le(altered,sector+16,0xffffffff,4); write(file,altered); target=ure::storage_image(file,sector);
            auto plan=ure::gpt_plan(target,"gpt.repair","fixture"); const auto hash=plan["plan_sha256"].asString();
            reject([&]{ure::gpt_execute(target,plan,base/"wrong","bad");},"confirmation-required");
            check(!ure::fs::exists(base/"wrong"),"Rejected confirmation created a journal");
            auto writable=ure::storage_image(file,sector,true);
            auto state=ure::gpt_execute(writable,ure::parse_json(ure::json(plan)),base/"repair",hash);
            check(state["state"]=="COMMITTED" && ure::gpt_inspect(writable.descriptor.get(),sector)["healthy"]==true,"GPT repair failed");
            auto inspected=ure::storage_image(file,sector);
            check(ure::gpt_journal_inspect(inspected,base/"repair")["classification"]=="TARGET_CONTENT_VERIFIED","GPT journal did not verify the target readback");
            ure::Root journal(base/"repair"); state["state"]="VERIFYING"; journal.save_record("journal.json",state,true);
            const auto unchanged_identity=inspected.identity;
            check(ure::gpt_resume(inspected,base/"repair",hash)["resume_readback_only"]==true,"GPT readback commit failed");
            check(ure::json(ure::storage_image(file,sector).identity)==ure::json(unchanged_identity),"Readback resume wrote the image");
            reject([&]{ure::gpt_resume(inspected,base/"repair",hash);},"unsafe-resume");
            {
                auto held=journal.open(".lock",O_RDWR); check(::flock(held.get(),LOCK_EX|LOCK_NB)==0,"Cannot acquire fixture journal lock");
                reject([&]{ure::gpt_journal_inspect(inspected,base/"repair");},"busy-journal");
            }
            change(file,2*sector+10,"X"); inspected=ure::storage_image(file,sector);
            check(ure::gpt_journal_inspect(inspected,base/"repair")["classification"]=="DIVERGED","Unrelated GPT write was not classified as divergence");
            auto changed=ure::storage_image(file,sector,true);
            reject([&]{ure::gpt_rollback(changed,base/"repair",hash);},"changed-target");
            change(file,2*sector,pristine.substr(2*sector,16384));
            // The primary is rebuilt from the backup, so usable data must remain exact.
            const auto first=inspection["primary"]["first_usable_lba"].asUInt64()*sector;
            const auto last=(inspection["primary"]["last_usable_lba"].asUInt64()+1)*sector;
            check(ure::storage_read(writable.descriptor.get(),first,static_cast<std::size_t>(last-first))==pristine.substr(static_cast<std::size_t>(first),static_cast<std::size_t>(last-first)),"Repair modified usable partition data");
            auto refreshed=ure::storage_image(file,sector,true);
            ure::gpt_rollback(refreshed,base/"repair",hash);
            check(ure::sha256(refreshed.descriptor.get())==ure::sha256(altered),"GPT rollback did not restore exact original metadata");
            target=ure::storage_image(file,sector); plan=ure::gpt_plan(target,"gpt.restore","fixture",base/"backup");
            writable=ure::storage_image(file,sector,true);
            state=ure::gpt_execute(writable,plan,base/"restore",plan["plan_sha256"].asString());
            check(state["state"]=="COMMITTED" && ure::sha256(writable.descriptor.get())==ure::sha256(pristine),"GPT restore failed");
            check(state["completed_ranges"][0]=="backup_table" && state["completed_ranges"][1]=="backup_header" && state["completed_ranges"][2]=="primary_table","Restore did not publish backup before primary");
            // Simulate a partially written range from the sealed before/after data.
            const auto before=ure::bounded_read(base/"restore/before-primary_header.bin",4096);
            change(file,sector,before.substr(0,18)); refreshed=ure::storage_image(file,sector,true);
            check(ure::gpt_journal_inspect(refreshed,base/"restore")["classification"]=="PARTIAL_EXPECTED_WRITE","Partial GPT write was not recognized");
            ure::gpt_rollback(refreshed,base/"restore",plan["plan_sha256"].asString());
            check(ure::sha256(refreshed.descriptor.get())==ure::sha256(altered),"Rollback after a partial metadata write failed");
            write(file,pristine); altered=pristine; altered[(256-1)*sector+16]^=1; write(file,altered);
            target=ure::storage_image(file,sector); plan=ure::gpt_plan(target,"gpt.repair","fixture"); writable=ure::storage_image(file,sector,true);
            ure::gpt_execute(writable,plan,base/"repair-backup",plan["plan_sha256"].asString());
            check(ure::gpt_inspect(writable.descriptor.get(),sector)["healthy"]==true,"Backup GPT repair failed");
            // A sealed plan must not let a changed source pass just because GUIDs match.
            write(file,altered); target=ure::storage_image(file,sector); plan=ure::gpt_plan(target,"gpt.repair","fixture");
            change(file,100*sector,"X"); writable=ure::storage_image(file,sector,true);
            reject([&]{ure::gpt_execute(writable,plan,base/"stale",plan["plan_sha256"].asString());},"stale-plan");
            // Correct hashes in a forged manifest cannot authorize non-GPT offsets.
            ure::Root backup(base/"backup"); auto forged=manifest; forged["regions"][0]["offset"]=Json::UInt64(100*sector);
            forged.removeMember("manifest_sha256"); forged["manifest_sha256"]=ure::sha256(ure::json(forged)); backup.save_record("manifest.json",forged,true);
            reject([&]{ure::gpt_backup_verify(base/"backup");},"invalid-backup");
            backup.save_record("manifest.json",manifest,true);
            change(base/"backup/manifest.json",ure::json(manifest).size()," ");
            reject([&]{ure::gpt_backup_verify(base/"backup");},"backup-corrupt");
            backup.save_record("manifest.json",manifest,true); change(base/"backup/primary_table.bin",0,"X");
            reject([&]{ure::gpt_backup_verify(base/"backup");},"backup-corrupt");
            // Both valid but differing tables cannot be selected automatically.
            altered=pristine; const auto backup_table=(256-1-16384/sector)*sector;
            altered[backup_table+56]='z'; const auto header=(256-1)*sector;
            le(altered,header+88,crc(altered.substr(backup_table,16384)),4); le(altered,header+16,0,4); le(altered,header+16,crc(altered.substr(header,92)),4);
            write(file,altered); target=ure::storage_image(file,sector);
            reject([&]{ure::gpt_plan(target,"gpt.repair","fixture");},"ambiguous-gpt");
            // Minimum reserved space, entry-size powers and unused-entry contents.
            altered=pristine; le(altered,sector+40,3,8); le(altered,sector+16,0,4); le(altered,sector+16,crc(altered.substr(sector,92)),4); write(file,altered);
            target=ure::storage_image(file,sector); check(!ure::gpt_inspect(target.descriptor.get(),sector)["primary"]["valid"].asBool(),"Undersized GPT reserve accepted");
            altered=pristine; le(altered,sector+84,136,4); le(altered,sector+16,0,4); le(altered,sector+16,crc(altered.substr(sector,92)),4); write(file,altered);
            target=ure::storage_image(file,sector); check(!ure::gpt_inspect(target.descriptor.get(),sector)["primary"]["valid"].asBool(),"Unsupported entry size accepted");
            // A valid older layout can put a table in the new layout's usable
            // space. Restoring its metadata must require a migration plan.
            auto relocated=pristine; auto entries=pristine.substr(2*sector,16384);
            le(entries,32,6+16384/sector,8); relocated.replace(6*sector,16384,entries);
            relocated.replace(backup_table,16384,entries);
            for(unsigned position:{sector,(256-1)*sector}) {
                le(relocated,position+40,6+16384/sector,8);
                if(position==sector)le(relocated,position+72,6,8);
                le(relocated,position+88,crc(entries),4); le(relocated,position+16,0,4);
                le(relocated,position+16,crc(relocated.substr(position,92)),4);
            }
            write(base/"geometry.img",relocated); auto geometry=ure::storage_image(base/"geometry.img",sector);
            ure::gpt_backup(geometry,base/"geometry-backup","fixture");
            write(base/"geometry.img",pristine); geometry=ure::storage_image(base/"geometry.img",sector);
            reject([&]{ure::gpt_plan(geometry,"gpt.restore","fixture",base/"geometry-backup");},"layout-change-required");
        }
        const auto system_tree=work/"system";
        write(system_tree/"sys/devices/test/block/sda/uevent","MAJOR=8\nMINOR=0\n"); write(system_tree/"sys/devices/test/block/sda/size","256\n");
        write(system_tree/"sys/devices/test/block/sda/queue/logical_block_size","4096\n"); write(system_tree/"dev/block/sda",image(4096));
        ure::fs::create_directories(system_tree/"sys/class/block"); ure::fs::create_symlink("../../devices/test/block/sda",system_tree/"sys/class/block/sda");
        ure::Root system(system_tree);
        reject([&]{ure::storage_select(system,"sysfs:sys/devices/test/block/sda");},"invalid-block");
        reject([&]{ure::storage_select(system,"sda");},"invalid-identity");
        std::cout<<"GPT tests: standards bounds, UTF-16, backup/compare, wrong targets/profiles, primary/backup repair, ordered restore, journal inspection/locks/divergence, readback commit, partial rollback, stale and forged plans, and live-node refusal passed\n";
        ure::fs::remove_all(work); return 0;
    } catch(const std::exception& error) { std::cerr<<"GPT test failure: "<<error.what()<<'\n'; ure::fs::remove_all(work); return 1; }
}
