// SPDX-License-Identifier: Apache-2.0
#include "management-hooks.h"
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
namespace {
void check(bool condition,const std::string& message) { if(!condition)throw std::runtime_error(message); }
std::string value(const std::string& key) { std::string out; DataManager::GetValue(key,out); return out; }
std::string digest(const ure::fs::path& path) { ure::Root parent(path.parent_path()); auto file=parent.open(path.filename().string(),O_RDONLY); return ure::sha256(file.get()); }
void write(const ure::fs::path& path,const std::string& text) { ure::fs::create_directories(path.parent_path()); std::ofstream file(path); file<<text; check(file.good(),"Cannot create GUI fixture"); }
}
int main(int argc,char* argv[]) {
    std::array<char,40> buffer{}; const std::string pattern="/tmp/ure-management-gui-XXXXXX"; std::copy(pattern.begin(),pattern.end(),buffer.begin());
    const auto* temporary=::mkdtemp(buffer.data()); if(!temporary)return 1; const ure::fs::path fixture(temporary);
    try {
        const auto image=fixture/"filesystem.img"; { std::ofstream file(image); file.seekp(32*1024*1024-1); file.put('\0'); }
        const auto original=digest(image); GUIAction action;
        for(const auto& [key,text]:std::map<std::string,std::string>{{"ure_raw_kind","image"},{"ure_raw_source",image.string()},{"ure_raw_sector","512"},
            {"ure_journal_parent",fixture.string()},{"ure_fs_action","format"},{"ure_fs_type","ext4"},{"ure_fs_erase","0"},{"ure_fs_label","URETEST"}})DataManager::SetValue(key,text);
        check(action.uremanager("filesystem-plan")==1 && digest(image)==original,"GUI format accepted missing data-loss choice");
        DataManager::SetValue("ure_fs_erase","1"); check(action.uremanager("filesystem-plan")==0 && value("ure_manage_hash").size()==64,"Actual GUI did not prepare a sealed plan");
        check(value("ure_manage_summary").find("Required journal space")!=std::string::npos && value("ure_manage_can_apply")=="1","GUI summary omitted capacity or eligibility");
        DataManager::SetValue("ure_fs_label","CHANGED"); check(action.uremanager("filesystem-execute")==1 && digest(image)==original,"Changed GUI choices reused a reviewed plan");
        DataManager::SetValue("ure_fs_label","URETEST"); check(action.uremanager("filesystem-execute")==0,"GUI image format did not complete");
        const auto formatted=digest(image); check(formatted!=original && value("ure_manage_hash").empty(),"GUI failed to invalidate a consumed review");
        check(action.uremanager("filesystem-journal-inspect")==0 && value("ure_fs_can_rollback")=="1","GUI did not expose verified rollback");
        DataManager::SetValue("ure_raw_source",(fixture/"another.img").string());
        check(action.uremanager("filesystem-rollback")==1 && digest(image)==formatted,"GUI journal review followed a changed target");
        DataManager::SetValue("ure_raw_source",image.string()); check(action.uremanager("filesystem-rollback")==0 && digest(image)==original,"Actual GUI did not restore every original byte");
        DataManager::SetValue("ure_form_field","ure_manage_hash"); DataManager::SetValue("ure_form_value","forged");
        check(action.uremanager("manage-field-save")==1 && value("ure_manage_hash").empty(),"Unselected editor field altered confirmation");
        const auto root=fixture/"linux"; for(const auto* directory:{"usr/bin","proc","sys","dev","tmp","run","etc"})ure::fs::create_directories(root/directory);
        write(root/"etc/os-release","ID=arch\nNAME=Arch fixture\n"); ure::fs::copy_file("/usr/bin/bash",root/"usr/bin/bash");
        for(const auto& [key,text]:std::map<std::string,std::string>{{"ure_root",root.string()},{"ure_esp",""},{"ure_rescue_action","shell"},{"ure_rescue_write","0"},{"ure_rescue_timeout","10"},{"ure_rescue_command","true"}})DataManager::SetValue(key,text);
        check(action.uremanager("rescue-plan")==0,"GUI did not detect the installed Arch rescue family");
        DataManager::SetValue("ure_rescue_command","changed command"); check(action.uremanager("rescue-execute")==1,"GUI reused review after its command changed");
        check(action.uremanager("linux-audit")==0 && ure::parse_json(value("ure_output"))["metadata_consistent"]==false,"Empty boot inventory was presented as consistent");
        DataManager::SetValue("ure_btrfs_root",fixture.string());
        DataManager::SetValue("ure_btrfs_action","create"); DataManager::SetValue("ure_btrfs_path","child");
        // The fixture may reside on Btrfs or tmpfs. No Btrfs executor is invoked.
        const auto info_status=action.uremanager("btrfs-info");
        if(info_status!=0) { const auto code=ure::parse_json(value("ure_output"))["error"]["code"].asString(); check(code=="not-btrfs" || code=="not-subvolume","GUI hid a genuine runtime failure"); }
        check(!ure::fs::exists(fixture/"child"),"Read-only GUI discovery changed storage");
        check(argc==2,"The combined partition fixture executable is required"); const auto disk=fixture/"layout.img";
        const auto child=::fork(); check(child>=0,"Cannot create GUI disk fixture");
        if(child==0) { ::execl(argv[1],argv[1],"--fixture",disk.c_str(),static_cast<char*>(nullptr)); ::_exit(127); }
        int status=0; check(::waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0,"GUI disk fixture failed");
        const auto original_disk=digest(disk);
        for(const auto& [key,text]:std::map<std::string,std::string>{{"ure_gpt_kind","image"},{"ure_gpt_source",disk.string()},{"ure_gpt_sector","4096"},
            {"ure_layout_mode","standard"},{"ure_layout_placement","after_userdata"},{"ure_layout_userdata_policy","preserve"},{"ure_layout_record_edits","[]"}})DataManager::SetValue(key,text);
        for(const auto* role:{"esp","linux","windows","userdata"}) {
            const std::string name=role; DataManager::SetValue("ure_layout_"+name+"_size",name=="userdata" ? "" : "64");
            DataManager::SetValue("ure_layout_"+name+"_unit",name=="userdata" ? "remaining" : "MiB"); DataManager::SetValue("ure_layout_"+name+"_guid","");
            DataManager::SetValue("ure_layout_"+name+"_filesystem",name=="esp" ? "fat32" : name=="windows" ? "ntfs" : "ext4");
        }
        check(action.uremanager("layout-plan")==0 && value("ure_gpt_plan_hash").size()==64 && value("ure_layout_review").find("Required journal space")!=std::string::npos,
            "Actual layout GUI did not review a combined filesystem/GPT job");
        DataManager::SetValue("ure_layout_linux_size","65"); check(action.uremanager("layout-apply-image")==1 && digest(disk)==original_disk,"Changed layout bypassed review");
        DataManager::SetValue("ure_layout_linux_size","64"); check(action.uremanager("layout-apply-image")==0 && value("ure_gpt_plan_hash").empty(),"Actual GUI did not complete the combined image job");
        check(action.uremanager("partition-job-inspect")==0 && value("ure_partition_can_rollback")=="1","Actual GUI did not inspect a complete data/GPT journal");
        DataManager::SetValue("ure_gpt_source",image.string()); check(action.uremanager("partition-job-rollback")==1,"Changed GUI target reused journal confirmation");
        DataManager::SetValue("ure_gpt_source",disk.string()); check(action.uremanager("partition-job-rollback")==0 && digest(disk)==original_disk,"Actual GUI failed complete partition rollback");
        ure::fs::remove_all(fixture); std::cout<<"Actual management callbacks: erase choice, sealed review, changed selections, image format/readback/rollback, rescue review, truthful empty audit and native Btrfs discovery passed; host UI stand-ins only.\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; ure::fs::remove_all(fixture); return 1; }
}
