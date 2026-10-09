// SPDX-License-Identifier: Apache-2.0
#include "lifecycle_policy.hpp"
#include "operation_lease.hpp"
#include <cstdio>
#include <mutex>
#include <unistd.h>

#ifndef __ANDROID__
namespace ure {
namespace {
struct Token {
    pid_t process=::getpid();
    LifecycleLease lease;
    explicit Token(LifecycleLease held):lease(std::move(held)) {}
    void verify() const { require(process==::getpid(),"operation-lease-inactive","A lifecycle token cannot be inherited by another process"); lease.require_active(); }
};
std::mutex reboot_mutex;
std::unique_ptr<Token> staged_reboot;
void refused(const char* code) { std::fprintf(stderr,"URE_LIFECYCLE_BLOCKED code=%s\n",code); }
}
void* lifecycle_acquire(const char* action,bool adopt_staged_reboot) noexcept {
    try {
        if(adopt_staged_reboot) {
            require(std::string_view(action)=="reboot","invalid-lifecycle-action","Only reboot consumes the staged reboot reservation");
            std::lock_guard<std::mutex> held(reboot_mutex);
            if(staged_reboot) {
                auto candidate=std::move(staged_reboot);
                candidate->verify();
                require(dualboot_quarantine_permits(action),"dualboot-quarantined","The dualboot checkpoint does not permit this lifecycle transition");
                return candidate.release();
            }
        }
        auto candidate=std::make_unique<Token>(LifecycleLease::acquire(action));
        require(dualboot_quarantine_permits(action),"dualboot-quarantined","The dualboot checkpoint does not permit this lifecycle transition");
        return candidate.release();
    } catch(const Error& error) { refused(error.code.c_str()); }
    catch(...) { refused("lifecycle-unavailable"); }
    return nullptr;
}
bool lifecycle_validate(void* token) noexcept {
    try { require(token!=nullptr,"operation-lease-inactive","A lifecycle token is required"); static_cast<Token*>(token)->verify(); return true; }
    catch(const Error& error) { refused(error.code.c_str()); } catch(...) { refused("lifecycle-unavailable"); }
    return false;
}
void lifecycle_release(void* token) noexcept { delete static_cast<Token*>(token); }
bool lifecycle_stage_reboot() noexcept {
    try {
        std::lock_guard<std::mutex> held(reboot_mutex);
        auto candidate=std::move(staged_reboot);
        if(!candidate)candidate=std::make_unique<Token>(LifecycleLease::acquire("reboot"));
        candidate->verify();
        require(dualboot_quarantine_permits("reboot"),"dualboot-quarantined","The dualboot checkpoint does not permit reboot");
        staged_reboot=std::move(candidate); return true;
    } catch(const Error& error) { refused(error.code.c_str()); } catch(...) { refused("lifecycle-unavailable"); }
    return false;
}
} // namespace ure
#endif
