// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <sstream>
#include <string>
namespace boot_hal_test {
inline int passthrough_calls = 0;
inline int aidl_declared_calls = 0;
inline int aidl_wait_calls = 0;
inline int aidl_from_binder_calls = 0;
inline int hidl_get_service_calls = 0;
inline int hidl_cast_11_calls = 0;
inline int hidl_cast_12_calls = 0;
inline bool aidl_declared = true;
inline bool aidl_binder_available = true;
inline bool hidl_service_available = true;
inline std::string diagnostics;
inline constexpr int passthrough_status = 71;
inline void reset(bool declared = true, bool binder = true, bool hidl = true) {
    passthrough_calls = aidl_declared_calls = aidl_wait_calls = aidl_from_binder_calls = 0;
    hidl_get_service_calls = hidl_cast_11_calls = hidl_cast_12_calls = 0;
    aidl_declared = declared;
    aidl_binder_available = binder;
    hidl_service_available = hidl;
    diagnostics.clear();
}
inline int resolution_calls() {
    return passthrough_calls + aidl_declared_calls + aidl_wait_calls +
        aidl_from_binder_calls + hidl_get_service_calls + hidl_cast_11_calls + hidl_cast_12_calls;
}
struct Log {
    std::ostringstream stream;
    template<class T> Log& operator<<(const T& value) { stream << value; return *this; }
    ~Log() { diagnostics += stream.str(); diagnostics += '\n'; }
};
}  // namespace boot_hal_test
