// SPDX-License-Identifier: GPL-3.0-or-later
// Host-only serialization of the same profile compiled into the supervisor.
#include "touch-profile.hpp"
#include <iostream>
int main() {
    for (const auto &pin : uke::touch::kImageFiles)
        std::cout << pin.path << '\t' << pin.sha256 << '\t' << pin.bytes << '\n';
}
