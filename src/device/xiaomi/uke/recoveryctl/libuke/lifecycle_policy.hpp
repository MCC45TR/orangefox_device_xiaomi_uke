// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "recovery_write_policy.hpp"
#include "job_registry.hpp"
#include "dualboot_quarantine.hpp"

namespace ure {
// Android management effects are unavailable until the same persistent domain
// is accepted for operation admission and every lifecycle transition. Stock
// recovery reboot/unmount also retain a cooperating runtime activity lock.
// That volatile lock does not enable Android's persistent operation backend.
constexpr bool device_operation_coordinator_accepted() noexcept { return false; }
#ifndef __ANDROID__
void* lifecycle_acquire(const char* action, bool adopt_staged_reboot) noexcept;
bool lifecycle_validate(void* token) noexcept;
void lifecycle_release(void* token) noexcept;
bool lifecycle_stage_reboot() noexcept;
#endif
class LegacyLifecycleGuard {
    void* token_ = nullptr;
    enum class Action { Invalid, Reboot, Unmount, Mount, Shutdown };
    Action action_=Action::Invalid;
    static Action selected_action(const char* action) noexcept {
        if(!action)return Action::Invalid;
        if(std::strcmp(action,"reboot")==0)return Action::Reboot;
        if(std::strcmp(action,"unmount")==0)return Action::Unmount;
        if(std::strcmp(action,"mount")==0)return Action::Mount;
        if(std::strcmp(action,"shutdown")==0)return Action::Shutdown;
        return Action::Invalid;
    }
    const char* action_name() const noexcept {
        switch(action_) {
            case Action::Reboot: return "reboot";
            case Action::Unmount: return "unmount";
            case Action::Mount: return "mount";
            case Action::Shutdown: return "shutdown";
            default: return nullptr;
        }
    }
#ifdef __ANDROID__
    RuntimeActivityLease runtime_;
#endif
#ifndef __ANDROID__
    bool owned_ = false;
#endif
public:
    explicit LegacyLifecycleGuard(const char* action, const LegacyLifecycleGuard* parent = nullptr,
                                  bool adopt_staged_reboot = false) noexcept {
        action_=selected_action(action);
        if(action_==Action::Invalid || (adopt_staged_reboot && action_!=Action::Reboot))return;
#ifdef __ANDROID__
        static_assert(!device_operation_coordinator_accepted(), "Connect the accepted Android coordinator to lifecycle admission before enabling it");
        if(parent) { if(parent->active())token_=parent->token_; }
        else {
            runtime_=runtime_lifecycle_acquire(adopt_staged_reboot);
            if(runtime_.valid())token_=&runtime_;
        }
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
        // Acquire or borrow the runtime exclusion before reading quarantine.
        // The writer holds the same exclusive lease until its final checkpoint.
        return token_ && static_cast<const RuntimeActivityLease*>(token_)->valid() && dualboot_quarantine_permits(action_name());
#else
        return token_ && lifecycle_validate(token_) && dualboot_quarantine_permits(action_name());
#endif
    }
};
inline bool stage_legacy_reboot() noexcept {
#ifdef __ANDROID__
    static_assert(!device_operation_coordinator_accepted(), "Bind Android's staged reboot to the accepted coordinator");
    auto& reboot=job_registry_detail::reboot_state(); std::lock_guard<std::mutex> held(reboot.mutex);
    if(reboot.staged.acquired()) {
        if(reboot.staged.valid() && dualboot_quarantine_permits("reboot"))return true;
        reboot.staged=RuntimeActivityLease{}; return false;
    }
    auto candidate=runtime_lifecycle_acquire(false);
    if(!candidate.valid() || !dualboot_quarantine_permits("reboot"))return false;
    reboot.staged=std::move(candidate); return true;
#else
    // The host bridge acquires and retains its persistent-domain token. Its
    // implementation must check quarantine after acquisition before staging.
    return lifecycle_stage_reboot();
#endif
}
} // namespace ure
