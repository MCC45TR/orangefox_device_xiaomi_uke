// SPDX-License-Identifier: Apache-2.0
#include "management-hooks.h"
#include <array>
#include <fcntl.h>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
namespace {
void check(bool condition,const std::string& message) { if(!condition)throw std::runtime_error(message); }
std::string value(const std::string& key) { std::string out; DataManager::GetValue(key,out); return out; }
std::array<std::string,6> digests(const ure::fs::path& directory) {
    std::array<std::string,6> out;
    for(unsigned lun=0;lun<6;++lun) { auto target=ure::storage_image(directory/("lun"+std::to_string(lun)+".img"),4096);
        out[lun]=ure::storage_image_range_digest(target.descriptor.get(),0,target.identity["bytes"].asUInt64()); } return out;
}
}
int main(int argc,char** argv) {
    ure::fs::path work;
    try {
        check(argc==4,"Provide stock fixture executable, pinned inputs and test-work parent");
        auto pattern=(ure::fs::absolute(argv[3])/"stock-gui-XXXXXX").string(); std::vector<char> name(pattern.begin(),pattern.end()); name.push_back('\0');
        const auto created=::mkdtemp(name.data()); check(created!=nullptr,"Cannot create stock GUI test workspace"); work=created; const auto images=work/"images";
        const auto child=::fork(); check(child>=0,"Cannot fork stock GUI fixture");
        if(child==0) { ure::Fd quiet(::open("/dev/null",O_WRONLY)); ::dup2(quiet.get(),STDOUT_FILENO);
            ::execl(argv[1],argv[1],"--fixture",argv[2],images.c_str(),static_cast<char*>(nullptr)); ::_exit(127); }
        int status=0; check(::waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0,"Cannot prepare stock GUI images");
        const auto before=digests(images); GUIAction action;
        for(const auto& [key,text]:std::map<std::string,std::string>{{"ure_stock_job_model","poco-pad-x1"},{"ure_stock_job_sku","fixture-declared"},
            {"ure_stock_inputs",(images/"inputs").string()},{"ure_stock_job_images",images.string()},{"ure_stock_job_originals",""},
            {"ure_stock_job_boot","0"},{"ure_stock_job_slots","a"},{"ure_stock_job_super","0"},{"ure_stock_job_reset","0"},{"ure_stock_job_zero","0"},{"ure_stock_job_whole_boot","0"},{"ure_journal_parent",work.string()}})
            DataManager::SetValue(key,text);
        check(action.uremanager("stock-job-plan")==0 && value("ure_stock_job_hash").size()==64 && value("ure_stock_job_can_execute")=="1","Actual GUI did not review all six LUNs");
        check(value("ure_stock_job_summary").find("LUN 5:")!=std::string::npos && value("ure_stock_job_summary").find("Required journal space")!=std::string::npos,"GUI summary omitted a LUN or staging budget");
        DataManager::SetValue("ure_stock_job_sku","changed-declaration");
        check(action.uremanager("stock-job-execute")==1 && value("ure_stock_job_hash").empty() && digests(images)==before,"Changed SKU reused a GUI confirmation");
        DataManager::SetValue("ure_stock_job_sku","fixture-declared"); DataManager::SetValue("ure_stock_job_boot","1");
        check(action.uremanager("stock-job-plan")==0 && ure::parse_json(value("ure_output"))["request"]["payloads"].size()==7,"GUI slot A boot set omitted an OS image");
        DataManager::SetValue("ure_stock_job_whole_boot","1");
        check(action.uremanager("stock-job-execute")==1 && value("ure_stock_job_hash").empty() && digests(images)==before,"Changed boot layout policy reused a GUI confirmation");
        check(action.uremanager("stock-job-plan")==0 && value("ure_stock_job_summary").find("reviewed-whole-partition")!=std::string::npos,"GUI omitted explicit whole boot layout review");
        const auto whole=ure::parse_json(value("ure_output"));
        for(const auto& row:whole["regions"])if(row["name"]=="dtbo_a")check(row["bytes"].asUInt64()==24*1024*1024 && row["programmed_observation"]["whole_partition_source_layout_verified"]==true,"GUI whole layout remained a prefix plan");
        DataManager::SetValue("ure_stock_job_whole_boot","0");
        DataManager::SetValue("ure_stock_job_slots","both"); check(action.uremanager("stock-choice-changed")==0 && value("ure_stock_job_hash").empty(),"Slot selector did not invalidate review");
        check(action.uremanager("stock-job-plan")==0 && ure::parse_json(value("ure_output"))["request"]["payloads"].size()==14,"GUI both-slot boot set was incomplete");
        DataManager::SetValue("ure_stock_job_boot","0"); DataManager::SetValue("ure_stock_job_reset","1");
        check(action.uremanager("stock-job-plan")==1 && value("ure_stock_job_can_execute").empty() && digests(images)==before,"GUI reset bypassed sparse zero policy");
        DataManager::SetValue("ure_stock_job_zero","1");
        check(action.uremanager("stock-job-plan")==1 && ure::parse_json(value("ure_output"))["error"]["code"]=="stock-capacity-mismatch" && digests(images)==before,"GUI inferred userdata capacity from ROM size");
        DataManager::SetValue("ure_stock_job_reset","0"); DataManager::SetValue("ure_stock_job_zero","0"); DataManager::SetValue("ure_stock_job_model","xiaomi-pad-7");
        check(action.uremanager("stock-job-plan")==0 && action.uremanager("stock-job-execute")==0 && value("ure_stock_job_hash").empty(),"Actual GUI did not commit the declared Pad 7 image job");
        const auto committed=digests(images); check(committed!=before && !value("ure_stock_job_journal").empty(),"GUI lost the retained recovery journal");
        check(action.uremanager("stock-job-inspect")==0 && value("ure_stock_job_can_rollback")=="1","Actual GUI did not inspect all originals for rollback");
        const auto journal=value("ure_stock_job_journal"); DataManager::SetValue("ure_stock_job_journal",(work/"wrong-journal").string());
        check(action.uremanager("stock-job-rollback")==1 && digests(images)==committed,"Changed journal path reused GUI confirmation");
        DataManager::SetValue("ure_stock_job_journal",journal);
        check(action.uremanager("stock-job-rollback")==0 && digests(images)==before && value("ure_stock_job_journal_hash").empty(),"Actual GUI failed complete six-LUN rollback");
        ure::fs::remove_all(work); std::cout<<"PASS actual stock GUI callbacks: six-LUN review, slot sets, reset/zero/capacity guards, model/SKU changes and full rollback; rendering and tablet HIL not run\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; if(!work.empty())ure::fs::remove_all(work); return 1; }
}
