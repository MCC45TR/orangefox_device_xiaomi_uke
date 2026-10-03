// SPDX-License-Identifier: Apache-2.0
#include "boot_fixture.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>
namespace {
using boot_fixture::check;
template<class F> void reject(F function,const std::string& code) {
    try { function(); } catch(const ure::Error& error) { check(error.code==code,"Unexpected error "+error.code+"; expected "+code); return; }
    throw std::runtime_error("Missing rejection: "+code);
}
std::string hash(const ure::fs::path& path) { ure::Root parent(path.parent_path()); auto fd=parent.open(path.filename().string(),O_RDONLY); return ure::sha256(fd.get()); }
ure::Value receipt(const ure::Value& handoff,const ure::Value& plan,const std::string& result) {
    ure::Value out; out["schema"]=1; out["request_id"]=plan["operation_id"]; out["attempt_id"]=handoff["state"]["attempt_id"];
    out["handoff_token"]=handoff["handoff_token"]; out["boot_id"]="deadbeef-1234-5678-9abc-def012345678";
    out["loader_sha256"]=plan["selected"]["loader_identity"]["sha256"]; out["result"]=result; return out;
}
// Stop the actual native process at syscall boundaries. The durable phase and
// real BootNext bytes choose the boundary; no production fault-injection hook.
void interrupt(const ure::fs::path& base,const ure::fs::path& journal,const ure::Value& plan,bool consume,bool retire=false) {
    const auto child=::fork(); check(child>=0,"Cannot fork interrupted boot job");
    if(child==0) {
        if(::ptrace(PTRACE_TRACEME,0,nullptr,nullptr)!=0)::_exit(81);
        ::raise(SIGSTOP);
        try {
            ure::Root esp(base/"esp"),variables(base/"variables");
            if(retire)ure::boot_route_action(esp,variables,journal,"cancel",plan["plan_sha256"].asString());
            else if(consume)ure::boot_route_action(esp,variables,journal,"consume-fixture",plan["plan_sha256"].asString());
            else ure::boot_route_execute(esp,variables,plan,journal,plan["plan_sha256"].asString());
            ::_exit(82);
        } catch(...) { ::_exit(83); }
    }
    int status=0; check(::waitpid(child,&status,0)==child && WIFSTOPPED(status),"Cannot trace child boot job"); bool killed=false;
    for(unsigned step=0;step<100000;++step) {
        if(::ptrace(PTRACE_SYSCALL,child,nullptr,nullptr)!=0)break;
        check(::waitpid(child,&status,0)==child,"Cannot wait for boot syscall"); if(!WIFSTOPPED(status))break;
        if(ure::fs::exists(journal/"state.json")) {
            try {
                const auto current=ure::json_file(journal/"state.json"); const bool next=ure::fs::exists(base/"variables"/boot_fixture::variable("BootNext"));
                const bool retirement=ure::fs::exists(base/"variables"/(".ure-boot-retired-"+plan["operation_id"].asString()+".json"));
                bool boundary=retire ? current["phase"]=="CANCELLED" && retirement && ure::fs::exists(base/"variables/.ure-boot-owner.json") :
                    consume ? current["phase"]=="CONSUMING" && !next : current["phase"]=="ARMING" && next &&
                    ure::fs::file_size(base/"variables"/boot_fixture::variable("BootNext"))==6;
                if(boundary) { check(::kill(child,SIGKILL)==0,"Cannot kill traced boot job"); killed=true; break; }
            } catch(const ure::Error&) { /* Initial record creation can be incomplete. */ }
        }
    }
    if(!killed) { ::kill(child,SIGKILL); ::waitpid(child,&status,0); throw std::runtime_error("Interrupted boot boundary was not reached"); }
    check(::waitpid(child,&status,0)==child && WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL,"Boot fixture did not terminate with SIGKILL");
}
} // namespace
int main(int argc,char* argv[]) {
    if(argc==3 && std::string(argv[1])=="--fixture") { try { boot_fixture::create(argv[2]); return 0; } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; } }
    std::array<char,40> name{}; const std::string pattern="/tmp/ure-boot-router-XXXXXX"; std::copy(pattern.begin(),pattern.end(),name.begin());
    const auto* temporary=::mkdtemp(name.data()); if(!temporary)return 1; const ure::fs::path work(temporary);
    try {
        boot_fixture::create(work); ure::Root esp(work/"esp"),variables(work/"variables");
        const auto order=work/"variables"/boot_fixture::variable("BootOrder"); const auto original_order=hash(order);
        const auto inventory=ure::boot_route_inventory(esp,variables);
        check(inventory["options"].size()==4 && inventory["default_option"]=="0000" && inventory["physical_device_write_allowed"]==false,"EFI inventory/default or evidence scope is incorrect");
        auto request=boot_fixture::request(); auto plan=ure::boot_route_plan(esp,variables,request);
        check(plan["selected"]["esp_partuuid"]==boot_fixture::partition_uuid && plan["fallback_boot_validated"]==false,"GPT identity decoding or fallback evidence boundary is incorrect");
        auto confirmation=plan["plan_sha256"].asString();
        reject([&] { ure::boot_route_execute(esp,variables,plan,work/"unconfirmed","wrong"); },"confirmation-required");
        check(!ure::fs::exists(work/"unconfirmed"),"Invalid confirmation created a journal");
        for(const auto& model:{"xiaomi-pad-7","poco-pad-x1"})for(const auto& profile:{"global-os3.0.303.0","cn-os3.0.302.0"}) {
            request["model"]=model; request["profile"]=profile; check(ure::boot_route_plan(esp,variables,request)["firmware_identity_verified"]==false,"A model/profile declaration claimed installed trust");
        }
        request=boot_fixture::request(); request["extra"]=true; reject([&] { ure::boot_route_plan(esp,variables,request); },"invalid-boot-request");
        request=boot_fixture::request(); request["target"]="windows"; reject([&] { ure::boot_route_plan(esp,variables,request); },"boot-target-mismatch");
        request=boot_fixture::request(); request["fallback_option"]="0003"; reject([&] { ure::boot_route_plan(esp,variables,request); },"unsafe-boot-fallback");
        request=boot_fixture::request(); request["esp_partuuid"]="00000000-1234-5678-9abc-def012345678"; reject([&] { ure::boot_route_plan(esp,variables,request); },"boot-components-missing");
        const auto linux_path=work/"esp/EFI/Linux/arch.efi"; boot_fixture::put(linux_path,boot_fixture::pe()+"changed");
        reject([&] { ure::boot_route_execute(esp,variables,plan,work/"stale",confirmation); },"stale-boot-assets"); check(!ure::fs::exists(work/"stale"),"Stale assets created a boot journal");
        boot_fixture::put(linux_path,boot_fixture::pe());
        const auto journal=work/"linux-job"; ure::boot_route_execute(esp,variables,plan,journal,confirmation);
        reject([&] { ure::boot_route_plan(esp,variables,boot_fixture::request()); },"boot-next-conflict");
        const auto handoff=ure::boot_route_action(esp,variables,journal,"consume-fixture",confirmation);
        check(handoff["state"]["attempts"]==1 && handoff["state"]["phase"]=="CONSUMED" && handoff["efi_application_started"]==false,"Consumption started an application or lost the single attempt");
        check(!variables.exists(boot_fixture::variable("BootNext")),"Consumed BootNext remained present");
        reject([&] { ure::boot_route_plan(esp,variables,boot_fixture::request()); },"boot-request-conflict");
        check(ure::bounded_read(journal/"state.json").find(handoff["handoff_token"].asString())==std::string::npos,"Plaintext handoff token was persisted");
        reject([&] { ure::boot_route_action(esp,variables,journal,"consume-fixture",confirmation); },"boot-already-consumed");
        auto ack=receipt(handoff,plan,"success"),wrong_ack=ack; wrong_ack["attempt_id"]="other-attempt";
        reject([&] { ure::boot_route_action(esp,variables,journal,"ack-fixture",confirmation,wrong_ack); },"boot-receipt-mismatch");
        wrong_ack=ack; wrong_ack["handoff_token"]=std::string(32,'0'); reject([&] { ure::boot_route_action(esp,variables,journal,"ack-fixture",confirmation,wrong_ack); },"boot-receipt-mismatch");
        const auto success=ure::boot_route_action(esp,variables,journal,"ack-fixture",confirmation,ack);
        check(success["state"]["phase"]=="ACKNOWLEDGED" && success["state"]["physical_boot_success"]==false,"A fixture acknowledgement claimed a physical boot");
        reject([&] { ure::boot_route_action(esp,variables,journal,"ack-fixture",confirmation,ack); },"invalid-boot-transition");
        check(ure::boot_route_history(journal)["events"].size()==5 && hash(order)==original_order,"History or immutable default was lost");
        reject([&] { ure::boot_route_execute(esp,variables,plan,work/"success-replay",confirmation); },"boot-plan-replayed");
        check(!ure::fs::exists(work/"success-replay") && !variables.exists(boot_fixture::variable("BootNext")),"Retired success created another journal or BootNext");
        // Windows uses the same durable contract; failures select the retained default.
        const auto windows_plan=ure::boot_route_plan(esp,variables,boot_fixture::request("windows","0002"));
        const auto wh=windows_plan["plan_sha256"].asString(); ure::boot_route_execute(esp,variables,windows_plan,work/"windows-job",wh);
        const auto windows_handoff=ure::boot_route_action(esp,variables,work/"windows-job","consume-fixture",wh);
        ure::boot_route_action(esp,variables,work/"windows-job","ack-fixture",wh,receipt(windows_handoff,windows_plan,"failure"));
        const auto fallback=ure::boot_route_action(esp,variables,work/"windows-job","fallback-fixture",wh);
        check(fallback["decision"]["number"]=="0000" && fallback["state"]["phase"]=="FALLBACK_SELECTED" && !variables.exists(boot_fixture::variable("BootNext")),"Fallback wrote or failed to preserve the Android default");
        reject([&] { ure::boot_route_execute(esp,variables,windows_plan,work/"fallback-replay",wh); },"boot-plan-replayed");
        // Android is selectable only against a distinct already registered default.
        const auto order_bytes=variables.read(boot_fixture::variable("BootOrder")); auto linux_default=order_bytes; boot_fixture::le(linux_default,4,3,2); boot_fixture::le(linux_default,10,0,2); boot_fixture::put(order,linux_default);
        const auto android_plan=ure::boot_route_plan(esp,variables,boot_fixture::request("android","0000","0003"));
        const auto ah=android_plan["plan_sha256"].asString(); ure::boot_route_execute(esp,variables,android_plan,work/"android-job",ah);
        check(ure::boot_route_action(esp,variables,work/"android-job","cancel",ah)["state"]["phase"]=="CANCELLED","Android fixture cancellation failed");
        reject([&] { ure::boot_route_execute(esp,variables,android_plan,work/"cancel-replay",ah); },"boot-plan-replayed"); boot_fixture::put(order,order_bytes);
        // A foreign request must survive both inspect and cancellation.
        plan=ure::boot_route_plan(esp,variables,boot_fixture::request()); confirmation=plan["plan_sha256"].asString();
        ure::boot_route_execute(esp,variables,plan,work/"cancel-job",confirmation); const auto next_path=work/"variables"/boot_fixture::variable("BootNext");
        const auto owned=variables.read(boot_fixture::variable("BootNext")); auto foreign=owned; foreign[4]=2; boot_fixture::put(next_path,foreign);
        reject([&] { ure::boot_route_action(esp,variables,work/"cancel-job","cancel",confirmation); },"boot-next-conflict"); check(variables.read(boot_fixture::variable("BootNext"))==foreign,"Foreign BootNext was removed");
        boot_fixture::put(next_path,owned); ure::boot_route_action(esp,variables,work/"cancel-job","cancel",confirmation);
        // Equal BootNext values are not proof of request ownership.
        plan=ure::boot_route_plan(esp,variables,boot_fixture::request()); confirmation=plan["plan_sha256"].asString();
        ure::boot_route_execute(esp,variables,plan,work/"owner-job",confirmation);
        const auto owner_path=work/"variables/.ure-boot-owner.json"; const auto original_owner=ure::json_file(owner_path);
        auto other_owner=original_owner; other_owner["request_id"]="another-request"; ure::save_json(owner_path,other_owner,true);
        reject([&] { ure::boot_route_action(esp,variables,work/"owner-job","cancel",confirmation); },"boot-request-conflict");
        check(variables.exists(boot_fixture::variable("BootNext")),"Equal bytes let an old journal cancel another owner");
        ure::save_json(owner_path,original_owner,true); ure::boot_route_action(esp,variables,work/"owner-job","cancel",confirmation);
        // A retirement record survives process death before owner removal.
        plan=ure::boot_route_plan(esp,variables,boot_fixture::request()); confirmation=plan["plan_sha256"].asString();
        ure::boot_route_execute(esp,variables,plan,work/"interrupted-retire",confirmation);
        interrupt(work,work/"interrupted-retire",plan,false,true);
        check(ure::boot_route_action(esp,variables,work/"interrupted-retire","recover",confirmation)["state"]["phase"]=="CANCELLED","Interrupted retirement lost the committed cancellation");
        check(!variables.exists(".ure-boot-owner.json"),"Recovered retirement retained ownership");
        reject([&] { ure::boot_route_execute(esp,variables,plan,work/"retirement-replay",confirmation); },"boot-plan-replayed");
        // Kill the real process after writing BootNext, before committing ARMED.
        plan=ure::boot_route_plan(esp,variables,boot_fixture::request()); confirmation=plan["plan_sha256"].asString();
        interrupt(work,work/"interrupted-arm",plan,false);
        check(ure::boot_route_action(esp,variables,work/"interrupted-arm","recover",confirmation)["state"]["phase"]=="ARMED","Interrupted arming did not recover from actual matching bytes");
        // Kill it after removal but before committing CONSUMED. Never replay it.
        interrupt(work,work/"interrupted-arm",plan,true);
        const auto unknown=ure::boot_route_action(esp,variables,work/"interrupted-arm","recover",confirmation);
        check(unknown["state"]["phase"]=="UNKNOWN" && unknown["state"]["physical_boot_success"]==false,"Interrupted handoff invented a boot result");
        reject([&] { ure::boot_route_action(esp,variables,work/"interrupted-arm","consume-fixture",confirmation); },"boot-already-consumed");
        ure::boot_route_action(esp,variables,work/"interrupted-arm","fallback-fixture",confirmation);
        plan=ure::boot_route_plan(esp,variables,boot_fixture::request()); confirmation=plan["plan_sha256"].asString();
        ure::boot_route_execute(esp,variables,plan,work/"no-ack",confirmation);
        ure::boot_route_action(esp,variables,work/"no-ack","consume-fixture",confirmation);
        check(ure::boot_route_action(esp,variables,work/"no-ack","recover",confirmation)["state"]["phase"]=="UNKNOWN","Consumed but unacknowledged request was replayed or treated as a proven failure");
        ure::boot_route_action(esp,variables,work/"no-ack","fallback-fixture",confirmation);
        reject([&] { ure::boot_route_execute(esp,variables,plan,work/"unknown-replay",confirmation); },"boot-plan-replayed");
        // A normal armed request disappearing after re-entry is also unknown.
        plan=ure::boot_route_plan(esp,variables,boot_fixture::request()); confirmation=plan["plan_sha256"].asString();
        ure::boot_route_execute(esp,variables,plan,work/"missing-next",confirmation); ure::fs::remove(next_path);
        const auto inspected=ure::boot_route_action(esp,variables,work/"missing-next","inspect");
        check(inspected["state"]["phase"]=="ARMED" && inspected["observation_is_boot_failure"]==false,"Read-only inspection changed history or inferred failure");
        check(ure::boot_route_action(esp,variables,work/"missing-next","recover",confirmation)["state"]["phase"]=="UNKNOWN","Missing BootNext was replayed or marked failed");
        ure::boot_route_action(esp,variables,work/"missing-next","fallback-fixture",confirmation);
        // Corrupted history, hard-linked records and unmarked runtime stores fail closed.
        auto broken=ure::json_file(journal/"state.json"); broken["events"][0]["reason"]="modified"; ure::save_json(journal/"state.json",broken,true);
        reject([&] { ure::boot_route_history(journal); },"invalid-boot-state");
        ure::fs::create_hard_link(work/"windows-job/state.json",work/"linked-state"); reject([&] { ure::boot_route_history(work/"windows-job"); },"unsafe-boot-record"); ure::fs::remove(work/"linked-state");
        ure::fs::remove(work/"variables/.ure-efi-fixture.json"); plan=ure::boot_route_plan(esp,variables,boot_fixture::request());
        check(plan["fixture_execute_allowed"]==false,"Unmarked EFI runtime was authorized");
        reject([&] { ure::boot_route_execute(esp,variables,plan,work/"runtime-write",plan["plan_sha256"].asString()); },"boot-backend-unverified");
        check(!ure::fs::exists(work/"runtime-write") && !ure::fs::exists(next_path) && hash(order)==original_order,"Runtime rejection or recovery changed firmware default");
        ure::fs::remove(work/"variables/.ure-boot-lock");
        reject([&] { ure::boot_route_action(esp,variables,work/"missing-next","inspect"); },"boot-backend-unverified");
        check(!variables.exists(".ure-boot-lock"),"Unaccepted runtime inspection created a lock file before checking its write gate");
        reject([&] { ure::management_dispatch({"boot","route-history",(work/"missing-next").string(),"--confirm","unused"}); },"invalid-options");
        // Independently malformed source nodes and architectures never become ready options.
        const auto option_path=work/"variables"/boot_fixture::variable("Boot0001"); const auto original_option=variables.read(boot_fixture::variable("Boot0001"));
        boot_fixture::put(option_path,boot_fixture::load_option("\\EFI\\..\\secret.efi","escape"));
        check(ure::boot_route_inventory(esp,variables)["options"][1]["components_verified"]==false,"Device-path traversal was accepted");
        boot_fixture::put(option_path,original_option.substr(0,14)); check(ure::boot_route_inventory(esp,variables)["options"][1]["components_verified"]==false,"Truncated EFI option was accepted");
        auto reserved=original_option; boot_fixture::le(reserved,4,0x80000001,4); boot_fixture::put(option_path,reserved);
        check(ure::boot_route_inventory(esp,variables)["options"][1]["components_verified"]==false,"Reserved load-option attributes were accepted");
        boot_fixture::put(option_path,original_option); auto x86=boot_fixture::pe(); boot_fixture::le(x86,132,0x8664,2); boot_fixture::put(linux_path,x86);
        check(ure::boot_route_inventory(esp,variables)["options"][1]["components_verified"]==false,"x86 EFI loader passed ARM64 selection");
        boot_fixture::put(linux_path,boot_fixture::pe()); auto duplicate=order_bytes; boot_fixture::le(duplicate,6,0,2); boot_fixture::put(order,duplicate);
        reject([&] { ure::boot_route_inventory(esp,variables); },"invalid-boot-order"); boot_fixture::put(order,order_bytes);
        std::cout<<"Native one-shot EFI file fixtures: exact GPT/loader/default bindings, Android/Linux/Windows decisions, single consumption, private correlated acknowledgements, cross-journal replay/conflict/corruption refusal, real SIGKILL at arming/consumption/retirement boundaries and truthful unknown/fallback history passed. No EFI application, real variable write or tablet boot occurred.\n";
        ure::fs::remove_all(work); return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; ure::fs::remove_all(work); return 1; }
}
