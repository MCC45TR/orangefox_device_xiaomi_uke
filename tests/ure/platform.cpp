// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
namespace {
using ure::Value;
unsigned controls=0;
void check(bool condition,const char* message) { ure::require(condition,"fixture-failure",message); }
template<class Function> void reject(Function function,const char* code) {
    try { function(); } catch(const ure::Error& error) { check(error.code==code,"Unexpected refusal code"); ++controls; return; }
    throw ure::Error("fixture-failure","Unsafe platform declaration was accepted");
}
void put(const ure::fs::path& path,const std::string& bytes) {
    ure::fs::create_directories(path.parent_path()); std::ofstream stream(path); stream<<bytes;
    check(stream.good(),"Cannot write private fixture");
}
bool blocker(const Value& row,const std::string& code) {
    for(const auto& item:row["blockers"])if(item==code)return true;
    return false;
}
void held(const Value& report) {
    check(report["physical_test_record"]==false && report["live_action_allowed"]==false && report["decision"]=="hold" &&
        report["credential_use_allowed"]==false && report["mapper_creation_allowed"]==false && report["encrypted_mount_allowed"]==false,
        "Platform report granted physical authority");
    check(report["features"].size()==11,"Platform capability family disappeared");
    for(const auto& feature:report["features"])check(feature["live_action_allowed"]==false && feature["state"]=="unavailable" &&
        blocker(feature,"exact-unit-profile-unaccepted") && blocker(feature,"platform-backend-unaccepted") && blocker(feature,"health-admission-unaccepted"),
        "A successful fixture enabled a platform feature");
}
Value boot() {
    Value result; result["available"]=true; result["consistent_reads"]=true; result["slot_suffix"]="_a";
    for(const auto& [key,value]:std::array<std::pair<const char*,const char*>,4>{{{"get-number-slots","2"},{"get-current-slot","0"},{"get-active-boot-slot","0"},{"get-snapshot-merge-status","none"}}}) {
        result[key]["available"]=true; result[key]["value"]=value;
    }
    for(unsigned index=0;index<2;++index) {
        Value row; row["slot"]=index;
        for(const auto* key:{"bootable","successful"}) { row[key]["available"]=true; row[key]["value"]="1"; }
        result["slots"].append(row);
    }
    return result;
}
std::pair<Value,Value> declarations(const Value& capabilities) {
    Value contract; contract["schema"]=1; contract["format"]="ure-platform-contract";
    auto& context=contract["context"]; context["profile"]="fixture-os3";
    for(const auto* key:{"commercial_model","model_number","device","product","hardware_sku","vendor_sku","firmware_version","build_fingerprint"})context["identity"][key]=std::string("synthetic-")+key;
    for(const auto* key:{"unit_identity_sha256","boot_id_sha256","kernel_sha256"})context[key]=ure::sha256(key);
    Value observation; observation["schema"]=1; observation["format"]="ure-platform-observation";
    observation["context"]=context; observation["boot_control"]=boot();
    for(const auto& name:capabilities["required_evidence"]) {
        Value requirement; requirement["check"]=name; requirement["artifact_sha256"]=ure::sha256(name.asString());
        contract["requirements"].append(requirement);
        requirement["context_sha256"]=ure::sha256(ure::json(context)); requirement["passed"]=true;
        observation["evidence"].append(requirement);
    }
    return {contract,observation};
}
}
int main() {
    std::array<char,64> name{}; const std::string pattern="/tmp/ure-platform-XXXXXX"; std::copy(pattern.begin(),pattern.end(),name.begin());
    const auto* directory=::mkdtemp(name.data()); if(!directory)return 1; const ure::fs::path work(directory);
    try {
        const auto tree=work/"root"; ure::fs::create_directories(tree); ure::Root root(tree);
        auto caps=ure::platform_capabilities(root,"fixture-os3"); held(caps);
        check(caps["current_system_root"]==false && caps["health"]["threshold_policy_configured"]==false &&
            blocker(caps,"platform-observation-root-untrusted"),"Imported root became hardware authority");
        auto [contract,observation]=declarations(caps);
        auto report=ure::platform_compare_fixture(contract,observation); held(report);
        check(report["fixture_checks_passed"]==true && report["comparison_is_artifact_verification"]==false && report["comparison_is_authentication"]==false,
            "Exact declared context failed or claimed authentication");
        for(const auto& feature:report["features"])check(feature["fixture_requirements_match"]==true,"A fully matched prerequisite family was lost");
        for(const auto* key:{"commercial_model","model_number","device","product","hardware_sku","vendor_sku","firmware_version","build_fingerprint"}) {
            auto changed=observation; changed["context"]["identity"][key]="foreign";
            report=ure::platform_compare_fixture(contract,changed); held(report);
            check(report["fixture_checks_passed"]==false && blocker(report,"platform-context-mismatch"),"Wrong commercial or firmware context passed"); ++controls;
        }
        for(const auto* key:{"unit_identity_sha256","boot_id_sha256","kernel_sha256"}) {
            auto changed=observation; changed["context"][key]=ure::sha256("foreign");
            report=ure::platform_compare_fixture(contract,changed);
            check(report["fixture_checks_passed"]==false,"Foreign unit, boot or kernel context passed"); ++controls;
        }
        for(const auto* state:{"unknown","merging","snapshotted","cancelled","NONE"," none","","invented"}) {
            auto changed=observation; changed["boot_control"]["get-snapshot-merge-status"]["value"]=state;
            report=ure::platform_compare_fixture(contract,changed); held(report);
            check(report["fixture_checks_passed"]==false && blocker(report,"virtual-ab-not-idle"),"Unsafe or malformed merge state passed"); ++controls;
        }
        for(const auto* key:{"get-number-slots","get-current-slot","get-active-boot-slot","get-snapshot-merge-status"}) {
            auto changed=observation; changed["boot_control"][key]["available"]=false;
            check(ure::platform_compare_fixture(contract,changed)["fixture_checks_passed"]==false,"Failed query with cached valid text passed"); ++controls;
        }
        for(const auto* flag:{"available","consistent_reads"}) {
            auto changed=observation; changed["boot_control"][flag]=false;
            check(ure::platform_compare_fixture(contract,changed)["fixture_checks_passed"]==false,"Unstable read sequence passed"); ++controls;
        }
        {
            auto changed=observation; changed["boot_control"]["slot_suffix"]="_b";
            check(blocker(ure::platform_compare_fixture(contract,changed),"active-slot-inconsistent"),"Conflicting boot suffix passed"); ++controls;
            changed=observation; changed["boot_control"]["get-active-boot-slot"]["value"]="1";
            check(blocker(ure::platform_compare_fixture(contract,changed),"pending-slot-transition"),"Pending boot transition passed"); ++controls;
        }
        for(const auto* key:{"bootable","successful"}) {
            auto changed=observation; changed["boot_control"]["slots"][1][key]["value"]="0";
            check(blocker(ure::platform_compare_fixture(contract,changed),"stock-slot-health-unverified"),"Inactive fallback without boot health passed"); ++controls;
        }
        for(Json::ArrayIndex index=0;index<observation["evidence"].size();++index)for(const auto* key:{"artifact_sha256","context_sha256","passed"}) {
            auto changed=observation;
            if(std::string(key)=="passed")changed["evidence"][index][key]=false;
            else changed["evidence"][index][key]=ure::sha256("foreign");
            report=ure::platform_compare_fixture(contract,changed); held(report);
            check(report["fixture_checks_passed"]==false,"Unbound, stale or failed evidence passed"); ++controls;
        }
        auto changed=observation; changed["evidence"]=Value(Json::arrayValue);
        check(ure::platform_compare_fixture(contract,changed)["fixture_checks_passed"]==false,"Absent evidence passed"); ++controls;
        changed=observation; changed["evidence"].append(changed["evidence"][0]);
        reject([&] { ure::platform_compare_fixture(contract,changed); },"invalid-platform-declaration");
        changed=observation; changed["evidence"][1]=changed["evidence"][0];
        reject([&] { ure::platform_compare_fixture(contract,changed); },"invalid-platform-declaration");
        changed=observation; changed["live_action_allowed"]=true;
        reject([&] { ure::platform_compare_fixture(contract,changed); },"invalid-platform-declaration");
        changed=observation; changed["evidence"][0]["passed"]="true";
        reject([&] { ure::platform_compare_fixture(contract,changed); },"invalid-platform-declaration");
        changed=observation; changed["context"]["identity"]["hardware_sku"]=std::string(513,'x');
        reject([&] { ure::platform_compare_fixture(contract,changed); },"invalid-platform-declaration");
        changed=observation; changed["boot_control"]["slots"][1]["slot"]=0;
        reject([&] { ure::platform_compare_fixture(contract,changed); },"invalid-platform-declaration");
        changed=contract; changed["requirements"][1]=changed["requirements"][0];
        reject([&] { ure::platform_compare_fixture(changed,observation); },"invalid-platform-declaration");
        for(const auto& feature:caps["features"])reject([&] { ure::platform_require_live_action(feature["id"].asString()); },"platform-action-unavailable");
        reject([&] { ure::platform_require_live_action("advanced"); },"invalid-platform-feature");
        // Actual bounded reader: class aliases, sparse attributes, unsafe file
        // kinds, oversized text and outside-root links. These are private trees.
        const auto battery=tree/"sys/devices/test-battery";
        put(battery/"type","Battery\n"); put(battery/"present","1\n"); put(battery/"capacity","75\n");
        put(battery/"status","Charging\n"); put(battery/"health","Good\n"); put(battery/"temp","250\n");
        ure::fs::create_directories(tree/"sys/class/power_supply"); ure::fs::create_symlink("../../devices/test-battery",tree/"sys/class/power_supply/battery");
        put(tree/"sys/class/thermal/thermal_zone0/type","vbat\n"); put(tree/"sys/class/thermal/thermal_zone0/temp","4100000\n");
        const auto ufs=tree/"sys/bus/platform/devices/synthetic.ufshc/health_descriptor";
        for(const auto* key:{"eol_info","life_time_estimation_a","life_time_estimation_b"})put(ufs/key,"0x01\n");
        caps=ure::platform_capabilities(root,"fixture-os3"); held(caps);
        check(caps["health"]["battery"][0]["capacity_valid"]==true && caps["health"]["thermal"][0]["calibrated_channel"]==false &&
            caps["health"]["ufs"].size()==1 && caps["health"]["decision"]=="hold","Telemetry forged safe channel or policy acceptance");
        put(battery/"capacity","101\n"); put(battery/"health","Overheat\n");
        caps=ure::platform_capabilities(root,"fixture-os3");
        check(blocker(caps["health"],"battery-capacity-unverified") && blocker(caps["health"],"battery-health-unverified"),"Invalid battery state passed"); ++controls;
        put(battery/"temp",std::string(257,'9')); caps=ure::platform_capabilities(root,"fixture-os3");
        check(caps["health"]["battery"][0]["temp"]["state"]=="malformed","Oversized attribute was read without refusal"); ++controls;
        ure::fs::remove(battery/"temp"); check(::mkfifo((battery/"temp").c_str(),0600)==0,"Cannot create FIFO");
        caps=ure::platform_capabilities(root,"fixture-os3"); check(caps["health"]["battery"][0]["temp"]["state"]=="malformed","FIFO telemetry was accepted"); ++controls;
        ure::fs::remove(battery/"temp"); put(work/"outside-secret","outside-root-private");
        ure::fs::create_symlink(work/"outside-secret",battery/"temp"); caps=ure::platform_capabilities(root,"fixture-os3");
        check(ure::json(caps).find("outside-root-private")==std::string::npos,"Telemetry leaked an outside-root symlink"); ++controls;
        const auto dispatched=ure::management_dispatch({"platform","preflight","--profile","fixture-os3","--system-root",tree.string()}); held(dispatched);
        reject([&] { ure::management_dispatch({"android","slot-set",(work/"unopened").string(),"--system-root",(work/"missing").string()}); },"platform-action-unavailable");
        std::cout<<"PASS "<<controls<<" platform negative controls; exact declarations, non-idle snapshots, slot/fallback, bound prerequisites, telemetry and pre-access refusal; no hardware acceptance\n";
        ure::fs::remove_all(work); return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; ure::fs::remove_all(work); return 1; }
}
