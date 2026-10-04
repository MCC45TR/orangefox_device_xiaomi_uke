// SPDX-License-Identifier: Apache-2.0
// Actual management callbacks and real private-image I/O. No GUI window,
// physical device, block device, fake backend or timing delay is used.
#include "management-hooks.h"
#include <fstream>
#include <iostream>
#include <unistd.h>
namespace {
void check(bool condition,const std::string& message) { if(!condition)throw std::runtime_error(message); }
std::string value(const std::string& key) { std::string text; DataManager::GetValue(key,text); return text; }
void image(const ure::fs::path& path,std::uint64_t bytes) {
    ure::Fd output(::open(path.c_str(),O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC,0600));
    check(output.get()>=0 && ::ftruncate(output.get(),static_cast<off_t>(bytes))==0,"Cannot create private image");
}
std::string digest(const ure::fs::path& path) { ure::Root root(path.parent_path()); auto file=root.open(path.filename().string(),O_RDONLY); return ure::sha256(file.get()); }
std::uint64_t ext4_bytes(int descriptor) {
    const auto super=ure::storage_read(descriptor,1024,1024);
    const auto word=[&](std::size_t offset) {
        std::uint64_t result=0; for(unsigned i=0;i<4;++i)result|=static_cast<std::uint64_t>(static_cast<unsigned char>(super[offset+i]))<<(8*i); return result;
    };
    check(static_cast<unsigned char>(super[56])==0x53 && static_cast<unsigned char>(super[57])==0xef && word(24)<=6,"Invalid independent ext4 size oracle");
    const auto blocks=word(4) | ((word(96)&0x80) ? word(336)<<32 : 0);
    return blocks*(std::uint64_t{1024}<<word(24));
}
void configure(const ure::fs::path& image,const ure::fs::path& work) {
    for(const auto& [key,text]:std::map<std::string,std::string>{{"ure_raw_kind","image"},{"ure_raw_source",image.string()},
        {"ure_raw_sector","512"},{"ure_journal_parent",work.string()},{"ure_backup_dir",(work/"backup").string()}})DataManager::SetValue(key,text);
}
struct Metrics { std::uint64_t admission_ms=0,status_ms=0,cancel_ms=0,elapsed_ms=0; unsigned samples=0; };
Metrics run(GUIAction& action,const std::string& command,bool change_selection=false,bool require_native_started=false) {
    const auto start=ure::monotonic_ms(); const auto admitted=action.uremanager(command); const auto admission=ure::monotonic_ms()-start;
    check(admitted==0,"Actual callback refused job admission: "+value("ure_job_notice"));
    const auto id=value("ure_job_id"); check(!id.empty() && value("ure_job_active")=="1","Admission did not expose a distinct owned job");
    Metrics result; result.admission_ms=admission; bool cancelled=false,observed_native=false;
    const auto deadline=start+120000;
    while(ure::monotonic_ms()<deadline) {
        const auto before=ure::monotonic_ms(); check(action.uremanager("job-status")==0,"Status could not reach the actual callback");
        result.status_ms=std::max(result.status_ms,ure::monotonic_ms()-before); ++result.samples;
        const auto status=ure::parse_json(value("ure_job_status"));
        check(status["job_id"]==id && status["backend_cleanup_verified"]==false,"Job status lost its ID or invented backend cleanup");
        if(status["state"]=="RUNNING" && ure::monotonic_ms()-start>=20)observed_native=true;
        if(observed_native && !cancelled && status["active"]==true) {
            if(change_selection)DataManager::SetValue("ure_raw_source","/changed-selection-must-not-be-opened");
            const auto before_cancel=ure::monotonic_ms(); check(action.uremanager("job-cancel")==0,"Actual callback could not acknowledge its owned stop request");
            result.cancel_ms=ure::monotonic_ms()-before_cancel; cancelled=true;
            const auto ack=ure::parse_json(value("ure_job_cancel_ack"));
            check(ack["acknowledgement"]=="ADVISORY_FLAG_ONLY" && ack["backend_cleanup_verified"]==false,"Stop acknowledgement pretended to clean up native I/O");
            // A second storage operation must be refused while work is owned.
            check(action.uremanager("load")==1,"A second management job replaced an active worker");
        }
        if(status["result_pending"]==true) {
            static_cast<void>(action.uremanager("job-collect"));
            const auto completion=ure::parse_json(value("ure_job_result"));
            check(completion["ready"]==true && completion["output"]["exit_code"].asInt()==0,"Actual native job failed: "+value("ure_job_result"));
            check(completion["apply_to_current_view"]==!change_selection,"An old selection was applied or a matching selection was discarded");
            check(completion["cancel_requested"]==cancelled,"The exact job lost its advisory request");
            const auto once=value("ure_job_result"); check(action.uremanager("job-collect")==0 && value("ure_job_result")==once,"A completion was applied twice");
            result.elapsed_ms=ure::monotonic_ms()-start; break;
        }
        ::poll(nullptr,0,2);
    }
    check(result.elapsed_ms>0 && result.samples>1,"Actual job did not finish under continued status sampling");
    check(result.admission_ms<250 && result.status_ms<250 && result.cancel_ms<250,"Actual job held a GUI state lock through native I/O");
    if(require_native_started)check(observed_native && cancelled && result.samples>=5,"The fixture was too short to measure running native work and stop acknowledgement");
    std::cout<<"METRIC command="<<command<<" admission_ms="<<result.admission_ms<<" max_status_ms="<<result.status_ms<<
        " cancel_ack_ms="<<result.cancel_ms<<" samples="<<result.samples<<" elapsed_ms="<<result.elapsed_ms<<
        " cooperative_ack_only=true actual_gui_frames=false physical_device=false\n";
    return result;
}
}
int main(int argc,char** argv) {
    auto pattern=(ure::fs::path(argc>1 ? argv[1] : ".")/"gui-latency-XXXXXX").string();
    std::vector<char> name(pattern.begin(),pattern.end()); name.push_back('\0'); const auto created=::mkdtemp(name.data()); if(!created)return 1;
    const ure::fs::path work(created);
    try {
        GUIAction action; const auto raw=work/"source.img"; image(raw,512*1024*1024); configure(raw,work);
        const auto original=digest(raw);
        static_cast<void>(run(action,"raw-plan",true,true));
        check(value("ure_backup_hash").empty() && value("ure_raw_source")=="/changed-selection-must-not-be-opened" && digest(raw)==original,
            "A completed read-only hash reset a changed selection, exposed its old review or wrote its source");
        DataManager::SetValue("ure_raw_source",raw.string());
        check(run_management(action,"raw-plan")==0 && value("ure_backup_hash").size()==64,"Cannot prepare the actual backup review");
        static_cast<void>(run(action,"raw-capture",false,true));
        check(ure::backup_verify(work/"backup")["verified"]==true && digest(raw)==original,"Actual backup after advisory stop did not independently verify");
        const auto filesystem=work/"ext4.img"; image(filesystem,320*1024*1024); configure(filesystem,work);
        for(const auto& [key,text]:std::map<std::string,std::string>{{"ure_fs_action","format"},{"ure_fs_type","ext4"},{"ure_fs_erase","1"},{"ure_fs_label","LATENCY"}})DataManager::SetValue(key,text);
        check(run_management(action,"filesystem-plan")==0 && run_management(action,"filesystem-execute")==0,"Cannot prepare private ext4 media");
        const auto formatted=digest(filesystem); DataManager::SetValue("ure_fs_action","resize"); DataManager::SetValue("ure_fs_size","160"); DataManager::SetValue("ure_fs_unit","MiB");
        check(run_management(action,"filesystem-plan")==0,"Cannot review the actual ext4 resize");
        static_cast<void>(run(action,"filesystem-execute",false,true));
        auto resized=ure::storage_image(filesystem,512);
        check(ext4_bytes(resized.descriptor.get())==160*1024*1024 && digest(filesystem)!=formatted,"Actual resizer did not persist its requested size");
        check(run_management(action,"filesystem-journal-inspect")==0 && value("ure_fs_can_rollback")=="1","Cannot independently review actual resize rollback");
        static_cast<void>(run(action,"filesystem-rollback",false,true));
        check(digest(filesystem)==formatted,"Actual rollback after advisory stop did not restore every original byte");
        check(management_foreign_reads==0 && management_foreign_writes==0,"A worker accessed mutable GUI variables");
        ure_gui_shutdown_jobs(); ure::fs::remove_all(work);
        std::cout<<"PASS actual management job latency and immutability: real 512 MiB hashing/backup, 320 MiB ext4 resize/rollback, independent digests/probe, stale-review refusal, single collection and no worker GUI access. Rendered frame, target and combined VM acceptance remain separate.\n"; return 0;
    } catch(const std::exception& error) {
        ure_gui_shutdown_jobs(); std::cerr<<error.what()<<"\nPrivate latency fixture retained: "<<work<<'\n'; return 1;
    }
}
