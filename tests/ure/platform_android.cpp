// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <algorithm>
#include <cstring>
#include <iostream>
namespace {
std::string merge="none";
bool changing=false,failed=false,fallback=false;
unsigned calls=0;
bool has_blocker(const ure::Value& report,const char* code) {
    for(const auto& item:report["blockers"])if(item==code)return true;
    return false;
}
}
// Only this host ABI test replaces the bounded reader. Android.bp has no
// injection target and no runtime environment/configuration overrides.
ure::ProcessResult wrapped_tool(const std::string&,const std::vector<std::string>&,int,std::string_view,const std::vector<int>&)
    asm("__wrap__ZN3ure8run_toolERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEERKSt6vectorIS5_SaIS5_EEiSt17basic_string_viewIcS3_ERKS8_IiSaIiEE");
ure::ProcessResult wrapped_tool(const std::string& name,const std::vector<std::string>& arguments,int timeout,
    std::string_view input,const std::vector<int>& descriptors) {
    ure::require(name=="bootctl" && timeout==2 && input.empty() && descriptors.empty() && !arguments.empty(),"fixture-failure","Unexpected platform tool or effect");
    const std::string command=arguments[0];
    const std::vector<std::string> allowed{"get-number-slots","get-current-slot","get-active-boot-slot","get-snapshot-merge-status","is-slot-bootable","is-slot-marked-successful"};
    ure::require(std::find(allowed.begin(),allowed.end(),command)!=allowed.end(),"fixture-failure","Read-only platform observer invoked a HAL mutation");
    ++calls; ure::ProcessResult result; result.status=0;
    if(command=="get-number-slots")result.output="2\n";
    else if(command=="get-current-slot")result.output=changing && calls>8 ? "1\n" : "0\n";
    else if(command=="get-active-boot-slot")result.output="0\n";
    else if(command=="get-snapshot-merge-status") { result.output=merge+"\n"; if(failed) { result.status=1; result.timed_out=true; } }
    else {
        ure::require(arguments.size()==2 && (arguments[1]=="0" || arguments[1]=="1"),"fixture-failure","Invalid slot-query identity");
        result.output=arguments[1]=="1" && !fallback ? "0\n" : "1\n";
        if(result.output=="0\n")result.status=70;
    }
    return result;
}
extern "C" int __system_property_get(const char* name,char* value) {
    std::strcpy(value,std::string_view(name)=="ro.boot.slot_suffix" ? "_a" : "synthetic-declaration");
    return static_cast<int>(std::strlen(value));
}
int main() {
    try {
        ure::Root current("/");
        for(const auto* state:{"none","unknown","merging","snapshotted","cancelled","bad"}) {
            merge=state; calls=0;
            const auto report=ure::platform_capabilities(current,"fixture-os3");
            ure::require(calls==12 && report["live_action_allowed"]==false && report["credential_use_allowed"]==false,
                "fixture-failure","Android observation changed tool budget or authorized credentials");
            ure::require(has_blocker(report,"virtual-ab-not-idle")==(merge!="none") && has_blocker(report,"stock-slot-health-unverified"),
                "fixture-failure","Android ABI reader misclassified merge/fallback state");
        }
        merge="none"; changing=true; calls=0;
        ure::require(has_blocker(ure::platform_capabilities(current,"fixture-os3"),"boot-control-observation-unavailable"),
            "fixture-failure","Android observer trusted changed slot reads");
        changing=false; failed=true; calls=0;
        ure::require(has_blocker(ure::platform_capabilities(current,"fixture-os3"),"virtual-ab-not-idle"),
            "fixture-failure","Timeout with cached idle text passed Android observer");
        failed=false; fallback=true; calls=0;
        const auto matched=ure::platform_capabilities(current,"fixture-os3");
        ure::require(!has_blocker(matched,"stock-slot-health-unverified") && matched["live_action_allowed"]==false,
            "fixture-failure","Bootable/successful fixture enabled a live Android action");
        for(const auto* id:{"android-fbe","android-slots","android-snapshots","android-super","android-ota","android-secondary","boot-once","boot-fallback","kernel-boot-chain","linux-repair","storage-write"}) {
            try { ure::platform_require_live_action(id); }
            catch(const ure::Error& error) { ure::require(error.code=="platform-action-unavailable","fixture-failure","Wrong shipping refusal"); continue; }
            throw ure::Error("fixture-failure","Shipping build enabled a platform writer");
        }
        try { ure::platform_compare_fixture(ure::Value(),ure::Value()); }
        catch(const ure::Error& error) {
            ure::require(error.code=="fixture-only-command","fixture-failure","Shipping declaration import reached schema or data access");
            std::cout<<"PASS Android-compiled read-only HAL allowlist, 12-query budget, merge/fallback/changed-read/timeout controls and all 11 feature refusals; host ABI fixture only\n";
            return 0;
        }
        throw ure::Error("fixture-failure","Shipping policy admitted imported declarations");
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
