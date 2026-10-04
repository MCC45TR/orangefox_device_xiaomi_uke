// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "recovery_write_policy.hpp"

namespace ure {
// Android management effects are unavailable until the same persistent domain
// is accepted for operation admission and every lifecycle transition. Stock
// recovery reboot/unmount remain usable while no such operation can exist.
constexpr bool device_operation_coordinator_accepted() noexcept { return false; }
#ifndef __ANDROID__
void* lifecycle_acquire(const char* action, bool adopt_staged_reboot) noexcept;
bool lifecycle_validate(void* token) noexcept;
void lifecycle_release(void* token) noexcept;
bool lifecycle_stage_reboot() noexcept;
#endif
class LegacyLifecycleGuard {
    void* token_ = nullptr;
#ifndef __ANDROID__
    bool owned_ = false;
#endif
public:
    explicit LegacyLifecycleGuard(const char* action, const LegacyLifecycleGuard* parent = nullptr,
                                  bool adopt_staged_reboot = false) noexcept {
#ifdef __ANDROID__
        static_assert(!device_operation_coordinator_accepted(), "Connect the accepted Android coordinator to lifecycle admission before enabling it");
        (void)action; (void)adopt_staged_reboot;
        if(!parent || parent->active())token_=this;
#else
        if(parent) { if(parent->active())token_=parent->token_; }
        else { token_=lifecycle_acquire(action,adopt_staged_reboot); owned_=token_!=nullptr; }
#endif
    }
    ~LegacyLifecycleGuard() {
#ifndef __ANDROID__
        if(owned_)lifecycle_release(token_);
#endif
    }
    LegacyLifecycleGuard(const LegacyLifecycleGuard&) = delete;
    LegacyLifecycleGuard& operator=(const LegacyLifecycleGuard&) = delete;
    bool active() const noexcept {
#ifdef __ANDROID__
        return token_!=nullptr;
#else
        return token_ && lifecycle_validate(token_);
#endif
    }
};
inline bool stage_legacy_reboot() noexcept {
#ifdef __ANDROID__
    static_assert(!device_operation_coordinator_accepted(), "Bind Android's staged reboot to the accepted coordinator");
    return true;
#else
    return lifecycle_stage_reboot();
#endif
}
} // namespace ure
