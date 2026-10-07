// SPDX-License-Identifier: Apache-2.0
#include "factory_harness.hpp"
#include <array>
#include <iostream>
#include <string_view>
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string_view mode(argv[1]);
    if (mode != "refusal" && mode != "baseline") return 2;
    struct Case { bool declared, binder, hidl; };
    constexpr std::array<Case,4> cases{{{true,true,true},{true,false,true},{false,true,true},{false,true,false}}};
    for (const auto& test : cases) {
        boot_hal_test::reset(test.declared, test.binder, test.hidl);
        const auto client = android::hal::BootControlClient::WaitForService();
        if (mode == "refusal") {
            if (client || boot_hal_test::resolution_calls() != 0 ||
                boot_hal_test::diagnostics.find(ure::legacy_write_decision(ure::LegacyWrite::PartitionMetadata).code) == std::string::npos) {
                std::cerr << "Boot client factory resolved a service or lost its shared-policy refusal.\n";
                return 1;
            }
        } else {
            const bool expected_client = test.declared ? test.binder : test.hidl;
            const int expected_casts = !test.declared && test.hidl ? 1 : 0;
            if (static_cast<bool>(client) != expected_client || boot_hal_test::passthrough_calls != 0 ||
                boot_hal_test::aidl_declared_calls != 1 ||
                boot_hal_test::aidl_wait_calls != (test.declared ? 1 : 0) ||
                boot_hal_test::aidl_from_binder_calls != (test.declared ? 1 : 0) ||
                boot_hal_test::hidl_get_service_calls != (test.declared ? 0 : 1) ||
                boot_hal_test::hidl_cast_11_calls != expected_casts || boot_hal_test::hidl_cast_12_calls != expected_casts) {
                std::cerr << "Unguarded factory baseline did not reach the expected AIDL or HIDL boundaries.\n";
                return 1;
            }
        }
    }
    std::cout << "PASS complete extracted production factory function: four " << mode << " cases.\n";
}
