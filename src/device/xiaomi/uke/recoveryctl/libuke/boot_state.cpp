// SPDX-License-Identifier: Apache-2.0
#include "boot_state.hpp"
#include "uke.h"
#ifdef __ANDROID__
#include <array>
#include <sys/system_properties.h>
#endif

namespace ure {
bool bootloader_unlocked(const Root& system) {
#ifdef __ANDROID__
    try {
        Root current("/");
        if (json(descriptor_identity(current.fd())) != json(descriptor_identity(system.fd()))) return false;
        auto property = [](const char* name) {
            std::array<char, PROP_VALUE_MAX> value{};
            __system_property_get(name, value.data());
            return std::string(value.data());
        };
        const auto config = system.exists("proc/bootconfig") ? system.read("proc/bootconfig", 256 * 1024) : "";
        return measured_uke_unlocked(property("ro.product.device"), property("ro.boot.vbmeta.device_state"),
            property("ro.boot.flash.locked"), property("ro.boot.verifiedbootstate"), config);
    } catch (const Error&) { return false; }
#else
    (void)system;
    return false;
#endif
}
}
