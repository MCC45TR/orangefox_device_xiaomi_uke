// SPDX-License-Identifier: Apache-2.0
#include "runtime.hpp"
#include <iostream>
#include <string_view>
int ure_boot_service_main(int, char**);
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string_view mode(argv[1]);
    if (mode != "refusal" && mode != "baseline") return 2;
    boot_hal_test::reset();
    const int status = ure_boot_service_main(0, nullptr);
    if (mode == "refusal") {
        if (status != 1 || boot_hal_test::resolution_calls() != 0) {
            std::cerr << "Boot service reached passthrough resolution or lost its refusal status.\n";
            return 1;
        }
        std::cout << "PASS complete service TU refused before the mocked passthrough boundary.\n";
    } else {
        if (status != boot_hal_test::passthrough_status || boot_hal_test::passthrough_calls != 1 ||
            boot_hal_test::resolution_calls() != 1) {
            std::cerr << "Unguarded service baseline did not reach the counted passthrough boundary.\n";
            return 1;
        }
        std::cout << "PASS unguarded service baseline reached the mocked passthrough boundary once.\n";
    }
}
