// SPDX-License-Identifier: Apache-2.0
#include "management-hooks.h"
#define main partition_fixture_main
#define URE_PARTITION_FIXTURE_CAPACITY_MIB 1024
#include "partition_job.cpp"
#undef main
namespace {
std::string gui_value(const char* key) { std::string result; DataManager::GetValue(key,result); return result; }
}
int main() {
    try {
        Workspace work; const auto image=work.path/"dualboot.img"; fixture(image,4096);
        const auto original=digest(image); GUIAction action;
        for(const auto& [key,text]:std::map<std::string,std::string>{{"tw_language","en"},{"ure_gpt_kind","image"},{"ure_gpt_source",image.string()},
            {"ure_gpt_sector","4096"},{"ure_journal_parent",work.path.string()},{"ure_db_linux","1"},{"ure_db_windows","0"},
            {"ure_db_esp","1"},{"ure_db_boot","0"},{"ure_db_userdata_fs","ext4"},{"ure_db_linux_fs","ext4"},
            {"ure_db_esp_size","512"},{"ure_db_esp_unit","MiB"},{"ure_db_linux_size","128"},{"ure_db_linux_unit","MiB"},
            {"ure_db_windows_size","64"},{"ure_db_windows_unit","MiB"},{"ure_db_linux_boot_size","64"},{"ure_db_linux_boot_unit","MiB"}})DataManager::SetValue(key,text);
        check(run_management(action,"db-preview")==0 && gui_value("ure_db_hash").size()==64,"Wizard did not prepare its exact plan");
        check(ure::parse_json(gui_value("ure_layout_graph"))["rows"].size()==4 && digest(image)==original,"Preview changed bytes or omitted its map");
        check(run_management(action,"db-apply")==1 && digest(image)==original,"Apply accepted missing data-loss consent");
        check(run_management(action,"db-toggle-esp")==0 && gui_value("ure_db_hash").empty(),"Changing ESP retained approval");
        check(run_management(action,"db-preview")==0,"Linux-only preview rejected optional ESP");
        const auto graph=ure::parse_json(gui_value("ure_layout_graph")); check(graph["rows"][1]["bytes"].isUInt64() && graph["rows"][1]["bytes"].asUInt64()==0,"Disabled ESP still consumes storage: "+ure::json(graph));
        check(run_management(action,"db-toggle-windows")==0 && gui_value("ure_db_esp")=="1","Windows did not require ESP");
        check(run_management(action,"db-toggle-esp")==1 && gui_value("ure_db_esp")=="1","Windows permitted ESP removal");
        check(run_management(action,"db-toggle-windows")==0,"Cannot disable Windows");
        DataManager::SetValue("ure_db_windows","1"); DataManager::SetValue("ure_db_esp","0");
        DataManager::SetValue("ure_db_linux","0"); DataManager::SetValue("ure_db_boot","1");
        check(run_management(action,"db-choice-changed")==0 && gui_value("ure_db_esp")=="1" && gui_value("ure_db_boot")=="0" && gui_value("ure_db_hash").empty(),
            "Actual checklist callback lost Windows/ESP or Linux/boot dependencies");
        DataManager::SetValue("ure_db_linux","1"); DataManager::SetValue("ure_db_windows","0");
        check(run_management(action,"db-choice-changed")==0,"Cannot restore Linux-only checkbox selection");
        check(run_management(action,"db-preview")==0,"Cannot refresh review");
        DataManager::SetValue("ure_db_linux_size","129"); DataManager::SetValue("ure_db_erase_ack","1"); DataManager::SetValue("ure_db_confirmation","ERASE USERDATA");
        check(run_management(action,"db-apply")==1 && digest(image)==original,"Changed sizes reused a prior approved plan");
        DataManager::SetValue("ure_db_linux_size","128"); check(run_management(action,"db-preview")==0,"Cannot review corrected selections");
        DataManager::SetValue("ure_db_erase_ack","1"); DataManager::SetValue("ure_db_confirmation","ERASE USERDATA");
        check(run_management(action,"db-apply")==0 && gui_value("ure_db_hash").empty(),"Confirmed image layout failed or kept reusable approval");
        ure::Root system("/"); auto writer=ure::storage_image(image,4096,true); const auto journal=gui_value("ure_partition_journal");
        const auto plan=ure::json_file(ure::fs::path(journal)/"plan.json");
        check(ure::partition_job_recover(system,writer,journal,"rollback",plan["plan_sha256"].asString())["state"]=="ROLLED_BACK" && digest(image)==original,"Wizard result cannot completely roll back its image");
        check(run_management(action,"db-discover")==1,"Host GUI automatically selected a real disk");
        std::cout<<"Dualboot GUI: optional ESP, Windows dependency, read-only preview, stale selection and consent refusal, image application and exact rollback passed.\n";
        ure_gui_shutdown_jobs(); return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; ure_gui_shutdown_jobs(); return 1; }
}
