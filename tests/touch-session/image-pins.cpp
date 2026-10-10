// SPDX-License-Identifier: GPL-3.0-or-later
// Host-only serialization of the same profile compiled into the supervisor.
#include "touch-profile.hpp"
#include <iostream>
int main(int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--kernel") {
        std::cout << uke::touch::kKernelRelease << '\n';
        return 0;
    }
    if (argc != 1)
        return 2;
    for (const auto &pin : uke::touch::kImageFiles)
        std::cout << pin.path << '\t' << pin.sha256 << '\t' << pin.bytes << '\n';
}
