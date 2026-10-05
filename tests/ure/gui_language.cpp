// SPDX-License-Identifier: Apache-2.0
// Actual GUI callback locale/view changes, with private regular-file media only.
#include "management-hooks.h"
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace {
void check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
std::string value(const char* key) { std::string text; DataManager::GetValue(key,text); return text; }
ure::Value collect(GUIAction& action,const std::string& id) {
    const auto deadline=ure::monotonic_ms()+10000;
    while(ure::monotonic_ms()<deadline) {
        static_cast<void>(action.uremanager("job-collect"));
        const auto bytes=value("ure_job_result");
        if(!bytes.empty()) {
            const auto result=ure::parse_json(bytes);
            if(result["ready"]==true && result["job_id"]==id)return result;
        }
        ::poll(nullptr,0,1);
    }
    throw std::runtime_error("Language context job did not complete");
}
std::string digest(const ure::fs::path& path) { ure::Root root(path.parent_path()); auto fd=root.open(path.filename().string(),O_RDONLY); return ure::sha256(fd.get()); }
}
int main(int argc,char** argv) {
    const auto work=ure::fs::path(argc>1 ? argv[1] : ".")/("gui-language-"+std::to_string(getpid()));
    try {
        check(ure::fs::create_directory(work),"Cannot create private language fixture");
        const auto image=work/"source.img", store=work/"backup";
        { ure::Fd fd(::open(image.c_str(),O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC,0600));
          check(fd.get()>=0 && ::ftruncate(fd.get(),4*1024*1024)==0,"Cannot create private image"); }
        const auto original=digest(image);
        for(const auto& [key,text]:std::map<std::string,std::string>{{"ure_raw_kind","image"},{"ure_raw_source",image.string()},
            {"ure_raw_sector","512"},{"ure_backup_dir",store.string()},{"tw_language","en"}})DataManager::SetValue(key,text);
        GUIAction action;
        check(action.uremanager("raw-plan")==0,"Cannot start original-language plan");
        const auto first=value("ure_job_id");
        DataManager::SetValue("tw_language","pt_BR");
        const auto stale=collect(action,first);
        check(stale["output"]["exit_code"]==0 && stale["apply_to_current_view"]==false && value("ure_backup_hash").empty(),
            "Completed old-language plan was published into the new language");
        check(value("tw_language")=="pt_BR" && value("ure_raw_source")==image.string() && digest(image)==original,
            "Discarding an old-language result changed native selections or source bytes");
        check(run_management(action,"raw-plan")==0 && value("ure_backup_hash").size()==64,"Cannot review current pt_BR plan");
        DataManager::SetValue("tw_language","pt_PT");
        check(run_management(action,"raw-capture")==1 && value("ure_backup_hash").empty() && !ure::fs::exists(store),
            "Regional language change reused the previous review or created backup output");
        check(run_management(action,"raw-plan")==0 && value("ure_backup_hash").size()==64 &&
            run_management(action,"raw-capture")==0,"Fresh current-language review did not allow the same native operation");
        check(ure::backup_verify(store)["verified"]==true && digest(image)==original,"Reviewed image backup failed byte verification");
        check(management_foreign_reads==0 && management_foreign_writes==0,"Language checks accessed GUI state from a worker");
        ure_gui_shutdown_jobs();
        ure::fs::remove_all(work);
        std::cout<<"Actual GUI language controls: stale completion discarded, pt_BR/pt_PT review invalidated, fresh review/backup verified; no translation meaning or tablet acceptance.\n";
    } catch(const std::exception& error) { ure_gui_shutdown_jobs(); std::cerr<<error.what()<<"\nPrivate language fixture retained: "<<work<<'\n'; return 1; }
}
