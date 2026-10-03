// SPDX-License-Identifier: Apache-2.0
#include "management-hooks.h"
#include "boot_fixture.h"
#include <array>
#include <cstdlib>
#include <iostream>
namespace {
std::string value(const std::string& name) { std::string out; DataManager::GetValue(name,out); return out; }
}
int main() {
    std::array<char,40> name{}; const std::string pattern="/tmp/ure-boot-gui-XXXXXX"; std::copy(pattern.begin(),pattern.end(),name.begin());
    const auto* temporary=::mkdtemp(name.data()); if(!temporary)return 1; const ure::fs::path work(temporary);
    try {
        boot_fixture::create(work); GUIAction action;
        for(const auto& [key,text]:std::map<std::string,std::string>{{"ure_boot_esp",(work/"esp").string()},{"ure_boot_variables",(work/"variables").string()},
            {"ure_boot_target","linux"},{"ure_boot_option","0001"},{"ure_boot_fallback","0000"},{"ure_boot_partuuid",boot_fixture::partition_uuid},
            {"ure_boot_model","poco-pad-x1"},{"ure_boot_profile","global-os3.0.303.0"},{"ure_journal_parent",work.string()}})DataManager::SetValue(key,text);
        boot_fixture::check(action.uremanager("boot-route-inventory")==0,"Actual GUI did not inventory registered EFI options");
        boot_fixture::check(action.uremanager("boot-route-stage-fixture")==1,"Unreviewed GUI staged a one-shot request");
        boot_fixture::check(action.uremanager("boot-route-plan")==0 && value("ure_boot_hash").size()==64 && value("ure_boot_can_stage")=="1","GUI failed exact request review");
        boot_fixture::check(value("ure_boot_summary").find("Preserved default: 0000")!=std::string::npos,"GUI summary omitted the retained default");
        DataManager::SetValue("ure_boot_model","xiaomi-pad-7");
        boot_fixture::check(action.uremanager("boot-route-stage-fixture")==1 && value("ure_boot_hash").empty(),"Changed model reused GUI review");
        boot_fixture::check(!ure::fs::exists(work/"variables"/boot_fixture::variable("BootNext")),"A stale GUI review wrote BootNext");
        boot_fixture::check(action.uremanager("boot-route-plan")==0 && action.uremanager("boot-route-stage-fixture")==0,"Actual GUI did not stage a private fixture request");
        const auto journal=value("ure_boot_journal"); boot_fixture::check(!journal.empty() && value("ure_boot_hash").empty(),"Consumed GUI review was retained");
        boot_fixture::check(action.uremanager("boot-journal-history")==0 && action.uremanager("boot-journal-inspect")==0 && value("ure_boot_can_cancel")=="1","GUI did not expose verified history and cancellation");
        DataManager::SetValue("ure_boot_esp",(work/"wrong-esp").string());
        boot_fixture::check(action.uremanager("boot-journal-cancel")==1 && ure::fs::exists(work/"variables"/boot_fixture::variable("BootNext")),"Changed ESP reused journal review");
        DataManager::SetValue("ure_boot_esp",(work/"esp").string());
        boot_fixture::check(action.uremanager("boot-journal-cancel")==0 && value("ure_boot_journal_hash").empty(),"GUI did not cancel its owned fixture request");
        boot_fixture::check(!ure::fs::exists(work/"variables"/boot_fixture::variable("BootNext")),"GUI cancellation left fixture BootNext");
        boot_fixture::check(action.uremanager("manage-edit-ure_boot_option")==0 && value("ure_form_back")=="ure_boot_manager","Boot input did not return to its page");
        DataManager::SetValue("ure_form_field","ure_boot_hash"); DataManager::SetValue("ure_form_value","forged");
        boot_fixture::check(action.uremanager("manage-field-save")==1 && value("ure_boot_hash").empty(),"Generic input changed a protected confirmation");
        ure::fs::remove(work/"variables/.ure-efi-fixture.json");
        boot_fixture::check(action.uremanager("boot-route-plan")==0 && value("ure_boot_can_stage")=="0","Unaccepted runtime store enabled a GUI writer");
        boot_fixture::check(action.uremanager("boot-route-stage-fixture")==1 && !ure::fs::exists(work/"variables"/boot_fixture::variable("BootNext")),"GUI bypassed the real-device boot gate");
        std::cout<<"Actual boot GUI callbacks: inventory, sealed target/model/default review, changed-context refusal, private fixture staging, journal history, cancellation, protected keyboard fields and real-routing refusal passed with host UI stand-ins. No window or tablet was tested.\n";
        ure::fs::remove_all(work); return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; ure::fs::remove_all(work); return 1; }
}
