// SPDX-License-Identifier: Apache-2.0
// Reuse disposable GPT/filesystem fixture construction, not a second executor.
#define main existing_partition_job_test_main
#define URE_PARTITION_FIXTURE_CAPACITY_MIB 1024
#include "partition_job.cpp"
#undef main

namespace {
ure::Value settings(bool linux_selected,bool windows,bool boot) {
    auto input=ure::parse_json(R"({"schema":1,"format":"uke-dualboot-request","linux_enabled":true,"windows_enabled":true,"esp_enabled":true,"separate_linux_boot":true,"userdata_policy":"recreate","userdata_filesystem":"ext4","esp":{"size":"512","unit":"MiB"},"linux_boot":{"size":"64","unit":"MiB"},"linux":{"size":"64","unit":"MiB","filesystem":"ext4"},"windows":{"size":"64","unit":"MiB"}})");
    input["linux_enabled"]=linux_selected; input["windows_enabled"]=windows; input["separate_linux_boot"]=boot;
    if(!linux_selected)input.removeMember("linux");
    if(!windows)input.removeMember("windows");
    if(!boot)input.removeMember("linux_boot");
    return input;
}
std::string enabled_order(const ure::Value& layout) {
    std::string result;
    for(const auto& row:layout["rows"])if(row["enabled"]==true) { if(!result.empty())result+=","; result+=row["role"].asString(); }
    return result;
}
}
int main(int argc,char* argv[]) {
    try {
        if(argc==3 && std::string_view(argv[1])=="--fixture-compatible") {
            fixture(argv[2],4096,false,false,true); return 0;
        }
        if(argc==2 && std::string_view(argv[1])=="--request") {
            std::cout<<ure::json(settings(true,true,true)); return 0;
        }
        check(argc==1,"Select --fixture-compatible IMAGE or --request; no argument runs the existing host tests");
        Workspace work; ure::Root system("/"); const auto disk=work.path/"disk.img"; fixture(disk,4096); auto source=ure::storage_image(disk,4096);
        const auto original=digest(disk);
        // Unsupported live policies must fail before GPT reads, tool discovery
        // or presenting a formatter list. This target has no usable descriptor.
        ure::StorageTarget inaccessible_live; inaccessible_live.identity["kind"]="live-block";
        for(const auto& [policy,filesystem]:std::array<std::pair<const char*,const char*>,3>{{{"preserve","f2fs"},{"preserve","ext4"},{"recreate","ext4"}}}) {
            auto unsupported=settings(true,false,false); unsupported["userdata_policy"]=policy; unsupported["userdata_filesystem"]=filesystem;
            reject([&]{ure::dualboot_plan(system,inaccessible_live,unsupported,"fixture");},"unsupported-live-dualboot-policy");
        }
        struct Selection { bool linux_selected,windows,boot; const char* order; };
        for(const auto& choice:std::array<Selection,5>{{{true,false,false,"userdata,esp,linux"},{true,false,true,"userdata,esp,linux_boot,linux"},
            {false,true,false,"userdata,esp,windows"},{true,true,false,"userdata,esp,linux,windows"},{true,true,true,"userdata,esp,linux_boot,linux,windows"}}}) {
            const auto plan=ure::dualboot_plan(system,source,settings(choice.linux_selected,choice.windows,choice.boot),"fixture");
            check(enabled_order(plan["gpt"]["layout"])==choice.order,"Wrong selected dualboot order");
            const auto text=ure::dualboot_preview(plan); check(text.find("Operations to review:")!=text.npos && text.find("ERASE USERDATA")!=text.npos,"Missing operation/data-loss review");
            const auto& layout=plan["gpt"]["layout"];
            check(layout["rows"][0]["offset"]==layout["pool"]["offset"] && layout["advanced_record_edits"].empty() && plan["writes_other_partition_payloads"]==false,"Scope escapes userdata");
            check(plan["physical_test_record"]==false,"False device acceptance");
        }
        auto bad=settings(false,false,false); reject([&]{ure::dualboot_plan(system,source,bad,"fixture");},"no-dualboot-system");
        bad=settings(false,true,true); reject([&]{ure::dualboot_plan(system,source,bad,"fixture");},"linux-boot-without-linux");
        bad=settings(true,false,false); bad["windows"]=settings(true,true,false)["windows"]; reject([&]{ure::dualboot_plan(system,source,bad,"fixture");},"disabled-dualboot-allocation");
        bad=settings(true,true,true); bad["esp"]["size"]="100"; bad["esp"]["unit"]="%"; reject([&]{ure::dualboot_plan(system,source,bad,"fixture");},"insufficient-layout-space");
        bad=settings(true,false,false); bad["esp"]["size"]="128"; reject([&]{ure::dualboot_plan(system,source,bad,"fixture");},"esp-too-small");
        bad=settings(false,true,false); bad["esp_enabled"]=false; bad.removeMember("esp");
        reject([&]{ure::dualboot_plan(system,source,bad,"fixture");},"windows-requires-esp");
        for(bool boot:{false,true}) {
            auto without=settings(true,false,boot); without["esp_enabled"]=false; without.removeMember("esp");
            const auto plan=ure::dualboot_plan(system,source,without,"fixture");
            check(enabled_order(plan["gpt"]["layout"])==(boot ? "userdata,linux_boot,linux" : "userdata,linux"),"Optional ESP changed the Linux layout");
            without["esp"]=settings(true,false,false)["esp"];
            reject([&]{ure::dualboot_plan(system,source,without,"fixture");},"disabled-dualboot-allocation");
        }
        bad=settings(true,false,false); bad["esp"]["size"]="1;reboot"; reject([&]{ure::dualboot_plan(system,source,bad,"fixture");},"invalid-layout-size");
        bad=settings(true,false,false); bad["record_edits"]=ure::Value(Json::arrayValue); reject([&]{ure::dualboot_plan(system,source,bad,"fixture");},"invalid-dualboot-request");
        for(const auto* unit:{"MB","MiB","GB","GiB","%"}) {
            auto input=settings(true,false,false); input["linux"]["unit"]=unit;
            input["linux"]["size"]=std::string(unit)=="%" ? "20" : (std::string(unit)=="GB" || std::string(unit)=="GiB") ? "0.1" : "100";
            const auto plan=ure::dualboot_plan(system,source,input,"fixture"); check(plan["gpt"]["layout"]["rows"][2]["enabled"]==true,"A required size unit failed");
        }
        check(digest(disk)==original,"Preview/refusal modified the original image");
        {
            auto changed=ure::dualboot_plan(system,source,settings(true,false,false),"fixture");
            changed["data_loss"]=false; changed["confirmation_phrase"]="PRESERVE USERDATA";
            changed.removeMember("plan_sha256"); changed["plan_sha256"]=ure::sha256(ure::json(changed));
            reject([&]{ure::dualboot_preview(changed);},"invalid-dualboot-plan");
            std::istringstream input("5\n1\next4\next4\n512 MiB\n64 MiB\n64 MiB\n64 MiB\n");
            std::ostringstream preview;
            const auto saved=work.path/"shell-plan.json";
            check(ure::dualboot_shell({"--image",disk.string(),"--sector-size","4096","--profile","fixture","--output",saved.string()},input,preview)==0,"Shell preview failed");
            check(enabled_order(ure::json_file(saved)["gpt"]["layout"])=="userdata,esp,linux_boot,linux,windows" &&
                preview.str().find("No application journal was selected")!=std::string::npos && digest(disk)==original,"Interactive preview changed the disk or selected the wrong layout");
            std::istringstream none; std::ostringstream unused;
            reject([&]{ure::dualboot_shell({},none,unused);},"fixture-only-command");
        }
        for(bool preserve:{false,true}) {
            auto input=settings(true,true,true); input["userdata_policy"]=preserve ? "preserve" : "recreate";
            auto selected=ure::storage_image(disk,4096); const auto plan=ure::dualboot_plan(system,selected,input,"fixture"); auto writer=ure::storage_image(disk,4096,true);
            const auto journal=work.path/(preserve ? "preserve" : "recreate");
            reject([&]{ure::dualboot_image_execute(system,writer,plan,journal,"wrong",plan["confirmation_phrase"].asString());},"confirmation-required");
            reject([&]{ure::dualboot_image_execute(system,writer,plan,journal,plan["plan_sha256"].asString(),"wrong");},"confirmation-required");
            check(!ure::fs::exists(journal),"Refused confirmation created a journal");
            const auto result=ure::dualboot_image_execute(system,writer,plan,journal,plan["plan_sha256"].asString(),plan["confirmation_phrase"].asString());
            check(result["state"]=="COMMITTED" && result["protected_ranges_verified"]==true,"Five-role dualboot transaction did not verify");
            const auto job_plan=ure::json_file(journal/"plan.json"); retained(disk,job_plan);
            check(ure::json(ure::json_file(journal/"dualboot-plan.json"))==ure::json(plan),"Journal lost the approved setup preview");
            check(ure::partition_job_recover(system,writer,journal,"rollback",job_plan["plan_sha256"].asString())["state"]=="ROLLED_BACK" && digest(disk)==original,"Dualboot rollback failed exact complete-byte comparison");
        }
        std::cout<<"Dualboot: five OS selections, optional Linux boot, MB/MiB/GB/GiB/percent, allocation/refusal controls, preview/data-policy confirmation, real five-role recreate/preserve jobs and exact rollback passed on regular images only.\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
