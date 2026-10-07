// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "runtime.hpp"
#include <memory>
namespace android {
template<class T> using sp = std::shared_ptr<T>;
namespace hardware::boot {
namespace V1_0 {
struct IBootControl {
    static sp<IBootControl> getService() {
        ++boot_hal_test::hidl_get_service_calls;
        return boot_hal_test::hidl_service_available ? std::make_shared<IBootControl>() : nullptr;
    }
};
}
namespace V1_1 {
struct IBootControl : V1_0::IBootControl {
    static sp<IBootControl> castFrom(const sp<V1_0::IBootControl>& module) {
        ++boot_hal_test::hidl_cast_11_calls;
        return module ? std::make_shared<IBootControl>() : nullptr;
    }
};
}
namespace V1_2 {
struct IBootControl : V1_1::IBootControl {
    static sp<IBootControl> castFrom(const sp<V1_0::IBootControl>& module) {
        ++boot_hal_test::hidl_cast_12_calls;
        return module ? std::make_shared<IBootControl>() : nullptr;
    }
};
}
}  // namespace hardware::boot
}  // namespace android
