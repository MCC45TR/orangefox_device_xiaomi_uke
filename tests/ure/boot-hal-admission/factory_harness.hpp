// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "hal_mock.hpp"
#include <android-base/logging.h>
#include <recovery_write_policy.hpp>
#include <cstdio>
namespace ndk {
struct SpAIBinder {
    void* value;
    explicit SpAIBinder(void* binder) : value(binder) {}
};
}
inline bool AServiceManager_isDeclared(const char*) {
    ++boot_hal_test::aidl_declared_calls;
    return boot_hal_test::aidl_declared;
}
inline void* AServiceManager_waitForService(const char*) {
    ++boot_hal_test::aidl_wait_calls;
    static int fake_binder;
    return boot_hal_test::aidl_binder_available ? &fake_binder : nullptr;
}
namespace aidl::android::hardware::boot {
struct IBootControl {
    static constexpr const char* descriptor = "android.hardware.boot.IBootControl";
    static std::shared_ptr<IBootControl> fromBinder(const ndk::SpAIBinder& binder) {
        ++boot_hal_test::aidl_from_binder_calls;
        return binder.value ? std::make_shared<IBootControl>() : nullptr;
    }
};
}
namespace android::hal {
namespace V1_0 = ::android::hardware::boot::V1_0;
namespace V1_1 = ::android::hardware::boot::V1_1;
namespace V1_2 = ::android::hardware::boot::V1_2;
struct BootControlClient {
    virtual ~BootControlClient() = default;
    static std::unique_ptr<BootControlClient> WaitForService();
};
struct BootControlClientAidl final : BootControlClient {
    explicit BootControlClientAidl(std::shared_ptr<::aidl::android::hardware::boot::IBootControl>) {}
};
struct BootControlClientHIDL final : BootControlClient {
    BootControlClientHIDL(sp<V1_0::IBootControl>, sp<V1_1::IBootControl>, sp<V1_2::IBootControl>) {}
};
}  // namespace android::hal
