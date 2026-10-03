// SPDX-License-Identifier: Apache-2.0
// Disposable sparse images. A host SIGKILL is not a tablet forced-reboot result.
#include "uke.h"
#include <algorithm>
#include <array>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

namespace {
constexpr std::array<std::uint64_t,6> capacities{32ULL<<30,16ULL<<20,16ULL<<20,4ULL<<20,2ULL<<30,128ULL<<20};
void check(bool condition,const std::string& message) { if(!condition)throw std::runtime_error(message); }
template<class F> void reject(F action,const std::string& code) {
    try { action(); } catch(const ure::Error& error) { check(error.code==code,"Unexpected refusal: "+error.code+", expected "+code); return; }
    throw std::runtime_error("Missing refusal: "+code);
}
void put(std::string& data,std::size_t offset,std::uint64_t value,unsigned bytes) { for(unsigned i=0;i<bytes;++i)data.at(offset+i)=static_cast<char>((value>>(8*i))&255); }
std::uint32_t crc(std::string_view data) { std::uint32_t out=UINT32_MAX;
    for(const auto byte:data) { out^=static_cast<unsigned char>(byte); for(unsigned i=0;i<8;++i)out=(out>>1)^((out&1U) ? 0xedb88320U : 0U); } return out^UINT32_MAX;
}
void write_bytes(int fd,std::string_view data,std::uint64_t offset) {
    check(::pwrite(fd,data.data(),data.size(),static_cast<off_t>(offset))==static_cast<ssize_t>(data.size()) && ::fsync(fd)==0,"Fixture write/sync failed");
}
void inputs_copy(const ure::fs::path& original,const ure::fs::path& path) {
    check(ure::fs::create_directory(path),"Cannot create private source fixture"); ure::fs::permissions(path,ure::fs::perms::owner_all);
    for(unsigned lun=0;lun<6;++lun)for(const auto* stem:{"gpt_main","gpt_backup","gpt_both","rawprogram","patch"}) {
        const auto name=std::string(stem)+std::to_string(lun)+(std::string(stem).starts_with("gpt_") ? ".bin" : ".xml");
        check(ure::fs::copy_file(original/name,path/name),"Cannot copy pinned GPT source fixture");
    }
    for(const auto* name:{"dtbo.img","vbmeta.img","vbmeta_system.img","metadata.img","boot.img","init_boot.img","recovery.img","vendor_boot.img"})
        check(ure::fs::copy_file(original/name,path/name),"Cannot copy selected pinned stock OS fixture");
}
ure::Value fixture(const ure::fs::path& original,const ure::fs::path& work,unsigned scale=1) {
    auto store=ure::private_directory(work,true); inputs_copy(original,work/"inputs");
    ure::Value request; request["schema"]=1; request["format"]="ure-stock-job-request"; request["model"]="poco-pad-x1"; request["sku"]="fixture-declared";
    request["firmware_profile"]="global-os3.0.303.0"; request["stock_inputs_directory"]=(work/"inputs").string(); request["erase_android_data"]=false; request["zero_sparse_holes"]=false;
    request["luns"]=ure::Value(Json::arrayValue); request["payloads"]=ure::Value(Json::arrayValue);
    for(unsigned lun=0;lun<6;++lun) {
        const auto capacity=capacities[lun]*scale; ure::Value source; auto ranges=ure::gpt_stock_regions(original,capacity,lun,ure::Value(),"global-os3.0.303.0",source);
        // Distinct original unit identities and an attribute that stock restores.
        for(auto& row:ranges)if(row.name=="primary_table" || row.name=="backup_table")row.bytes[48]^=0x10;
        const auto table_crc=crc(std::string_view(ranges[0].bytes).substr(0,(lun==4 ? 96U : 32U)*128));
        for(auto& row:ranges)if(row.name=="primary_header" || row.name=="backup_header") {
            row.bytes[56]^=static_cast<char>(lun+31); put(row.bytes,88,table_crc,4); put(row.bytes,16,0,4); put(row.bytes,16,crc(std::string_view(row.bytes).substr(0,92)),4);
        }
        const auto image=work/("lun"+std::to_string(lun)+".img"); ure::Fd file(::open(image.c_str(),O_RDWR|O_CREAT|O_EXCL,0600));
        check(file.get()>=0 && ::ftruncate(file.get(),static_cast<off_t>(capacity))==0,"Cannot create sparse LUN image");
        for(const auto& row:ranges)write_bytes(file.get(),row.bytes,row.offset);
        write_bytes(file.get(),"protected-lun-"+std::to_string(lun),32768);
        check(ure::gpt_inspect(file.get(),4096)["healthy"]==true,"Original stock fixture is invalid");
        ure::Value selected; selected["lun"]=lun; selected["image"]=image.string(); request["luns"].append(selected);
    }
    auto target=ure::storage_image(work/"lun4.img",4096,true); const auto table=ure::gpt_inspect(target.descriptor.get(),4096);
    for(const auto& row:table["partitions"])if(row["label"]=="dtbo_a") {
        const auto offset=row["start_lba"].asUInt64()*4096; write_bytes(target.descriptor.get(),std::string(1024*1024,'O'),offset);
        write_bytes(target.descriptor.get(),"tail-preserved",offset+20*1024*1024);
    }
    ure::save_json(work/"request.json",request); return request;
}
void payload(ure::Value& request,unsigned lun,const char* label,const char* filename) {
    ure::Value row; row["lun"]=lun; row["label"]=label; row["filename"]=filename; request["payloads"].append(row);
}
std::array<std::string,6> digests(const ure::Value& request) {
    std::array<std::string,6> out;
    for(unsigned lun=0;lun<6;++lun) { auto target=ure::storage_image(request["luns"][lun]["image"].asString(),4096);
        out[lun]=ure::storage_image_range_digest(target.descriptor.get(),0,target.identity["bytes"].asUInt64()); } return out;
}
void unchanged(const ure::Value& request,const std::array<std::string,6>& expected) { check(digests(request)==expected,"Full logical content of a LUN differs from its original"); }
ure::Value read(const ure::fs::path& path) { return ure::parse_json(ure::bounded_read(path,4*1024*1024)); }
void reseal(ure::Value& plan) { plan.removeMember("plan_sha256"); plan["plan_sha256"]=ure::sha256(ure::json(plan)); }
}
int main(int argc,char** argv) {
    ure::fs::path work;
    try {
        check(argc==3 || (argc==4 && std::string(argv[1])=="--fixture"),"Provide pinned inputs and a test-work parent, or --fixture INPUTS NEW_DIRECTORY");
        if(argc==4) {
            const auto request=fixture(ure::fs::absolute(argv[2]),ure::fs::absolute(argv[3])); std::cout<<ure::json(request); return 0;
        }
        const ure::fs::path original=ure::fs::absolute(argv[1]); std::string pattern=(ure::fs::absolute(argv[2])/"stock-job-tests-XXXXXX").string();
        std::vector<char> name(pattern.begin(),pattern.end()); name.push_back('\0'); const auto created=::mkdtemp(name.data()); check(created!=nullptr,"Cannot create stock test workspace"); work=created;
        for(unsigned scale:{1U,2U}) {
            auto request=fixture(original,work/("scale-"+std::to_string(scale)),scale); request["model"]=scale==1 ? "poco-pad-x1" : "xiaomi-pad-7";
            const auto before=digests(request); const auto plan=ure::stock_job_plan(request); const auto journal=ure::fs::path(request["luns"][0]["image"].asString()).parent_path()/"job";
            check(plan["luns"].size()==6 && plan["regions"].size()==30 && plan["model_identity_verified"]==false && plan["sku_capacity_verified"]==false &&
                plan["atomic_all_luns"]==false && plan["physical_test_record"]==false,"Stock job overstates source/model/atomicity scope");
            reject([&]{ure::stock_job_execute(plan,journal,"bad");},"confirmation-required"); unchanged(request,before);
            check(ure::stock_job_execute(plan,journal,plan["plan_sha256"].asString())["state"]=="COMMITTED","Six-LUN GPT job did not commit");
            check(ure::stock_job_recover(journal,"inspect")["classification"]=="TARGET_CONTENT_VERIFIED","Six-LUN readback was not verified");
            check(ure::stock_job_recover(journal,"rollback",plan["plan_sha256"].asString())["state"]=="ROLLED_BACK","Six-LUN rollback failed"); unchanged(request,before);
            reject([&]{ure::stock_job_recover(journal,"resume",plan["plan_sha256"].asString());},"unsafe-recovery");
        }
        auto request=fixture(original,work/"payload"); payload(request,4,"dtbo_a","dtbo.img"); payload(request,4,"vbmeta_a","vbmeta.img"); payload(request,0,"vbmeta_system_a","vbmeta_system.img");
        const auto before=digests(request); const auto plan=ure::stock_job_plan(request); const auto journal=work/"payload/job";
        check(ure::stock_job_execute(plan,journal,plan["plan_sha256"].asString())["state"]=="COMMITTED","Selected stock OS contents did not commit");
        for(const auto& row:plan["regions"])if(row["role"]=="payload") {
            auto target=ure::storage_image(request["luns"][row["lun"].asUInt()]["image"].asString(),4096);
            const auto check_path=work/("verify-"+row["name"].asString()+".img"); ure::Fd verified(::open(check_path.c_str(),O_RDWR|O_CREAT|O_EXCL,0600));
            check(verified.get()>=0,"Cannot create bounded payload verification image"); ure::storage_copy_image_range(target.descriptor.get(),verified.get(),row["offset"].asUInt64(),row["bytes"].asUInt64());
            check(ure::sha256(verified.get())==row["source_sha256"].asString(),"Raw stock programming differs from OEM input"); ure::fs::remove(check_path);
            if(row["name"]=="dtbo_a")check(ure::storage_read(target.descriptor.get(),row["offset"].asUInt64()+20*1024*1024,14)=="tail-preserved","DTBO unprogrammed capacity tail was overwritten");
        }
        // Offline recovery uses the journal rather than the original ROM files.
        ure::fs::rename(work/"payload/inputs",work/"payload/offline-inputs");
        check(ure::stock_job_recover(journal,"inspect")["classification"]=="TARGET_CONTENT_VERIFIED","Offline stock inspection failed");
        ure::stock_job_recover(journal,"rollback",plan["plan_sha256"].asString()); unchanged(request,before);
        ure::fs::rename(work/"payload/offline-inputs",work/"payload/inputs");
        auto bad=request; bad["model"]="unverified-donor"; reject([&]{ure::stock_job_plan(bad);},"invalid-stock-request");
        bad=request; bad["firmware_profile"]="cn-os3.0.302.0"; reject([&]{ure::stock_job_plan(bad);},"wrong-profile");
        bad=request; bad["luns"].resize(5); reject([&]{ure::stock_job_plan(bad);},"invalid-stock-request");
        bad=request; bad["luns"][5]["image"]=bad["luns"][0]["image"]; reject([&]{ure::stock_job_plan(bad);},"duplicate-stock-lun");
        bad=request; std::swap(bad["luns"][1]["image"],bad["luns"][2]["image"]); reject([&]{ure::stock_job_plan(bad);},"identity-unavailable");
        bad=request; payload(bad,4,"dtbo_a","dtbo.img"); reject([&]{ure::stock_job_plan(bad);},"duplicate-stock-payload");
        bad=request; payload(bad,1,"xbl_a","xbl.img"); reject([&]{ure::stock_job_plan(bad);},"protected-stock-payload");
        bad=request; bad["payloads"]=ure::Value(Json::arrayValue); payload(bad,0,"userdata","userdata.img"); reject([&]{ure::stock_job_plan(bad);},"userdata-reset-pair-required");
        payload(bad,0,"metadata","metadata.img"); bad["erase_android_data"]=true; reject([&]{ure::stock_job_plan(bad);},"stock-capacity-mismatch");
        bad=request; bad["payloads"]=ure::Value(Json::arrayValue); payload(bad,0,"metadata","metadata.img"); payload(bad,0,"userdata","userdata.img"); bad["erase_android_data"]=true;
        reject([&]{ure::stock_job_plan(bad);},"sparse-zero-policy-required");
        bad=request; bad["luns"][0]["image"]="/dev/null"; reject([&]{ure::stock_job_plan(bad);},"invalid-image");
        unchanged(request,before);
        const auto fresh_plan=ure::stock_job_plan(request); auto tampered=fresh_plan; tampered["regions"][0]["offset"]=Json::UInt64(0); reseal(tampered);
        reject([&]{ure::stock_job_execute(tampered,work/"invalid-shape",tampered["plan_sha256"].asString());},"invalid-stock-plan"); unchanged(request,before);
        const auto source=work/"payload/inputs/dtbo.img"; ure::Fd source_fd(::open(source.c_str(),O_RDWR|O_NOFOLLOW));
        const auto original_byte=ure::storage_read(source_fd.get(),0,1); write_bytes(source_fd.get(),"Q",0);
        reject([&]{ure::stock_job_execute(fresh_plan,work/"bad-source",fresh_plan["plan_sha256"].asString());},"stock-source-mismatch"); unchanged(request,before);
        check(ure::stock_job_recover(work/"bad-source","inspect")["classification"]=="ORIGINAL","Failed staging changed a LUN");
        check(ure::stock_job_recover(work/"bad-source","cancel",fresh_plan["plan_sha256"].asString())["state"]=="CANCELLED_SAFE","Failed staging could not be cancelled"); write_bytes(source_fd.get(),original_byte,0);
        // Kill a real child after a payload write has durably begun.
        auto interrupted=fixture(original,work/"interrupted"); payload(interrupted,4,"boot_a","boot.img"); const auto original_digests=digests(interrupted); const auto pending=ure::stock_job_plan(interrupted);
        const auto interrupted_journal=work/"interrupted/job"; const auto child=::fork(); check(child>=0,"Cannot fork interruption fixture");
        if(child==0) { try { ure::stock_job_execute(pending,interrupted_journal,pending["plan_sha256"].asString()); ::_exit(0); } catch(...) { ::_exit(2); } }
        bool killed=false; int status=0;
        for(unsigned attempt=0;attempt<30000;++attempt) {
            if(ure::fs::exists(interrupted_journal/"state.json")) {
                const auto state=read(interrupted_journal/"state.json");
                if(state["state"]=="APPLYING" && state["written_bytes"].asUInt64()>0 && state.isMember("last_verified_chunk")) {
                    check(::kill(child,SIGKILL)==0,"Cannot stop active stock writer"); killed=true; break;
                }
            }
            const auto done=::waitpid(child,&status,WNOHANG); if(done==child)throw std::runtime_error("Stock writer ended before interruption boundary");
            ::usleep(1000);
        }
        if(!killed) { ::kill(child,SIGKILL); ::waitpid(child,&status,0); throw std::runtime_error("Stock interruption boundary timed out"); }
        check(::waitpid(child,&status,0)==child && WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL,"Stock child was not killed at the intended boundary");
        const auto review=ure::stock_job_recover(interrupted_journal,"inspect"); check(review["classification"]=="EXPECTED_PARTIAL_WRITE","Interrupted stock application was not classified from bytes");
        ure::stock_job_recover(interrupted_journal,"resume",pending["plan_sha256"].asString());
        // A torn GPT write can be recovered even when both tables are unhealthy.
        ure::Root saved(interrupted_journal); const auto persisted=read(interrupted_journal/"plan.json");
        for(Json::ArrayIndex i=0;i<persisted["regions"].size();++i)if(persisted["regions"][i]["role"]=="gpt" && persisted["regions"][i]["lun"].asUInt()==4 &&
            (persisted["regions"][i]["name"]=="primary_table" || persisted["regions"][i]["name"]=="backup_table")) {
            const auto& row=persisted["regions"][i]; auto target=ure::storage_image(interrupted["luns"][4]["image"].asString(),4096,true);
            const auto old=saved.read("before-"+std::to_string(i)+".img",16384); write_bytes(target.descriptor.get(),std::string_view(old).substr(0,71),row["offset"].asUInt64());
        }
        check(ure::stock_job_recover(interrupted_journal,"inspect")["classification"]=="EXPECTED_PARTIAL_WRITE","Torn GPT mixture was not recognized");
        ure::stock_job_recover(interrupted_journal,"resume",pending["plan_sha256"].asString());
        auto target=ure::storage_image(interrupted["luns"][4]["image"].asString(),4096,true); const auto offset=persisted["regions"][0]["offset"].asUInt64();
        const auto desired=ure::storage_read(target.descriptor.get(),offset,1); ure::Root mirrors(interrupted_journal); auto previous_file=mirrors.open("before-0.img",O_RDONLY); const auto previous=ure::storage_read(previous_file.get(),0,1);
        char alien=1; while(alien==desired[0] || alien==previous[0])++alien; write_bytes(target.descriptor.get(),std::string(1,alien),offset);
        check(ure::stock_job_recover(interrupted_journal,"inspect")["classification"]=="DIVERGED","Unrelated stock payload divergence was accepted");
        reject([&]{ure::stock_job_recover(interrupted_journal,"resume",pending["plan_sha256"].asString());},"unsafe-recovery");
        reject([&]{ure::stock_job_recover(interrupted_journal,"rollback",pending["plan_sha256"].asString());},"unsafe-recovery");
        write_bytes(target.descriptor.get(),desired,offset);
        ure::stock_job_recover(interrupted_journal,"rollback",pending["plan_sha256"].asString()); unchanged(interrupted,original_digests);
        // A journal cannot be replayed on an identical copy with a different inode.
        const auto image=ure::fs::path(interrupted["luns"][5]["image"].asString()),copy=image.parent_path()/"clone.img";
        check(ure::fs::copy_file(image,copy),"Cannot create wrong-inode fixture"); ure::fs::rename(image,image.parent_path()/"original.img"); ure::fs::rename(copy,image);
        reject([&]{ure::stock_job_recover(interrupted_journal,"inspect");},"wrong-target");
        check(ure::management_dispatch({"stock","image-inspect",(original/"metadata.img").string()})["encoding"]=="android-sparse-v1","Stock CLI route bypassed source inspection");
        reject([&]{ure::management_dispatch({"stock","job-inspect",journal.string(),"--confirm","bad"});},"invalid-options");
        ure::fs::remove_all(work); std::cout<<"PASS six-LUN stock jobs, exact OS payload extents, offline rollback, hostile inputs, SIGKILL and torn GPT recovery\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; if(!work.empty())ure::fs::remove_all(work); return 1; }
}
