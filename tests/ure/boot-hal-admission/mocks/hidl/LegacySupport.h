// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "runtime.hpp"
namespace android::hardware {
// A counted boundary, not a replacement claim for the Android HAL loader.
template<class Interface, class ExpectedInterface = Interface>
int defaultPassthroughServiceImplementation() {
    ++boot_hal_test::passthrough_calls;
    return boot_hal_test::passthrough_status;
}
}  // namespace android::hardware
