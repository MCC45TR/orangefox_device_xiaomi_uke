// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <cstring>
#include <iostream>
namespace { int queries=0; }
extern "C" int __system_property_get(const char*,char* value) {
    ++queries; const char* supplied="uke-but-not-installed-firmware-proof";
    std::strcpy(value,supplied); return static_cast<int>(std::strlen(supplied));
}
int main() {
    try {
        ure::Root current("/");
        for(const auto* profile:{"fixture-accepted","global-os3.0.303.0","poco-os2.0.205.0","cn-os3.0.302.0"}) {
            const auto status=ure::device_profile_admission_status(current,profile);
            ure::require(status["live_plan_allowed"]==false && status["profile_accepted"]==false && status["accepted_live_profile_count"].asUInt64()==0 &&
                status["observations_are_installed_firmware_proof"]==false,"fixture-failure","Android property declarations enabled device admission");
        }
        ure::require(queries==28,"fixture-failure","Host policy did not use the expected Android property boundary");
        try { ure::device_profile_compare_fixture(ure::Value(),ure::Value()); }
        catch(const ure::Error& error) {
            ure::require(error.code=="fixture-only-command","fixture-failure","Android imported evidence failed at the wrong boundary");
            std::cout<<"PASS Android policy refuses imported declarations and synthetic accepted properties; host ABI fixture, not an Android target or tablet result\n";
            return 0;
        }
        throw std::runtime_error("Android accepted a fixture profile comparator");
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
